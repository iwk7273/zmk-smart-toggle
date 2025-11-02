#define DT_DRV_COMPAT zmk_behavior_smart_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define ZMK_BHV_MAX_ACTIVE_SMT_TOGS 5

struct behavior_smt_tog_config {
    int32_t ignored_key_positions_len;
    int32_t position_bindings_len;
    int32_t position_binding_behaviors_len;
    const uint8_t *ignored_key_positions;
    const uint32_t *position_bindings;
    const struct zmk_behavior_binding *position_binding_behaviors;
    struct zmk_behavior_binding tog_behavior;
    struct zmk_behavior_binding continue_behavior;
};

struct active_smt_tog {
    bool is_active;
    uint32_t position;
    zmk_keymap_layer_id_t layer;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    struct k_work release_work;
    bool release_pending;
    const struct behavior_smt_tog_config *config;
};

struct suppressed_smt_key {
    bool active;
    uint32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
};

static struct suppressed_smt_key suppressed_smt_keys[ZMK_BHV_MAX_ACTIVE_SMT_TOGS] = {};

void release_tog_behavior(struct active_smt_tog *st) {
    struct zmk_behavior_binding_event event = {
        .layer = st->layer,
        .position = st->position,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = st->source,
#endif
    };

    zmk_behavior_queue_add(&event, st->config->tog_behavior, false, 0);
}

struct active_smt_tog active_smt_togs[ZMK_BHV_MAX_ACTIVE_SMT_TOGS] = {};

static struct active_smt_tog *find_smt_tog(uint32_t position) {
    for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
        if (active_smt_togs[i].position == position && active_smt_togs[i].is_active) {
            return &active_smt_togs[i];
        }
    }
    return NULL;
}

static struct suppressed_smt_key *
find_suppressed_smt_key(uint32_t position
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                        , uint8_t source
#endif
) {
    for (int i = 0; i < ARRAY_SIZE(suppressed_smt_keys); i++) {
        if (!suppressed_smt_keys[i].active) {
            continue;
        }
        if (suppressed_smt_keys[i].position != position) {
            continue;
        }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        if (suppressed_smt_keys[i].source != source) {
            continue;
        }
#endif
        return &suppressed_smt_keys[i];
    }
    return NULL;
}

static void suppress_position_event(struct zmk_position_state_changed *ev) {
    struct suppressed_smt_key *entry =
        find_suppressed_smt_key(ev->position
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                                , ev->source
#endif
        );
    if (entry == NULL) {
        for (int i = 0; i < ARRAY_SIZE(suppressed_smt_keys); i++) {
            if (suppressed_smt_keys[i].active) {
                continue;
            }
            entry = &suppressed_smt_keys[i];
            break;
        }
    }
    if (entry == NULL) {
        LOG_WRN("Unable to suppress position %d, suppression list full", ev->position);
        return;
    }
    entry->active = true;
    entry->position = ev->position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    entry->source = ev->source;
#endif
}

static const struct zmk_behavior_binding *
find_position_behavior(const struct behavior_smt_tog_config *config, uint32_t position) {
    if (config->position_bindings_len == 0 || config->position_bindings == NULL ||
        config->position_binding_behaviors == NULL) {
        return NULL;
    }
    for (int i = 0; i < config->position_bindings_len; i++) {
        if (config->position_bindings[i] != position) {
            continue;
        }
        if (i >= config->position_binding_behaviors_len) {
            LOG_WRN("position binding missing behavior at index %d for behavior %p", i, config);
            return NULL;
        }
        return &config->position_binding_behaviors[i];
    }
    return NULL;
}

static int new_smt_tog(struct zmk_behavior_binding_event *event,
                       const struct behavior_smt_tog_config *config,
                       struct active_smt_tog **smt_tog) {
    for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
        struct active_smt_tog *const ref_smt_tog = &active_smt_togs[i];
        if (!ref_smt_tog->is_active && !ref_smt_tog->release_pending) {
            ref_smt_tog->position = event->position;
            ref_smt_tog->layer = event->layer;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            ref_smt_tog->source = event->source;
#endif
            ref_smt_tog->config = config;
            ref_smt_tog->is_active = true;
            ref_smt_tog->release_pending = false;
            *smt_tog = ref_smt_tog;
            return 0;
        }
    }
    return -ENOMEM;
}

static void smt_tog_release_work_handler(struct k_work *work) {
    struct active_smt_tog *smt_tog = CONTAINER_OF(work, struct active_smt_tog, release_work);
    release_tog_behavior(smt_tog);
    smt_tog->release_pending = false;
}

static void queue_smt_tog_release(struct active_smt_tog *smt_tog) {
    smt_tog->is_active = false;
    if (smt_tog->release_pending) {
        return;
    }
    smt_tog->release_pending = true;
    k_work_submit(&smt_tog->release_work);
}

static bool is_position_ignored(struct active_smt_tog *smt_tog, int32_t position) {
    if (find_position_behavior(smt_tog->config, position) != NULL) {
        return true;
    }
    if (smt_tog->position == position) {
        return true;
    }
    if (smt_tog->config->ignored_key_positions_len == 0 ||
        smt_tog->config->ignored_key_positions == NULL) {
        return false;
    }
    for (int i = 0; i < smt_tog->config->ignored_key_positions_len; i++) {
        if (smt_tog->config->ignored_key_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static int on_smt_tog_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_smt_tog_config *cfg = dev->config;
    struct active_smt_tog *smt_tog = find_smt_tog(event.position);
    if (smt_tog == NULL) {
        if (new_smt_tog(&event, cfg, &smt_tog) == -ENOMEM) {
            LOG_ERR("Unable to create new smart toggle. Insufficient space in "
                    "active_smt_togs[].");
            return ZMK_BEHAVIOR_OPAQUE;
        }
        LOG_DBG("created new smart toggle at pos %d", event.position);
        zmk_behavior_invoke_binding(&cfg->tog_behavior, event, true);
    }
    LOG_DBG("pressed smart toggle at pos %d", event.position);
    zmk_behavior_invoke_binding(&cfg->continue_behavior, event, true);

    // edge case: layer deactivation and its position release event might have happened before this was called
    if (!zmk_keymap_layer_active(smt_tog->layer)) {
        LOG_DBG("deactivate smart toggle at pos %d because of missed events", event.position);
        zmk_behavior_invoke_binding(&cfg->tog_behavior, event, false);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_smt_tog_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_smt_tog_config *cfg = dev->config;
    LOG_DBG("smart toggle keybind released at pos %d", event.position);
    zmk_behavior_invoke_binding(&cfg->continue_behavior, event, false);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_smt_tog_init(const struct device *dev) {
    static bool init_first_run = true;
    if (init_first_run) {
        for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
            active_smt_togs[i].is_active = false;
            active_smt_togs[i].release_pending = false;
            k_work_init(&active_smt_togs[i].release_work, smt_tog_release_work_handler);
        }
    }
    init_first_run = false;
    return 0;
}

static int smt_tog_position_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    struct suppressed_smt_key *suppressed =
        find_suppressed_smt_key(ev->position
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                                , ev->source
#endif
        );
    if (suppressed != NULL) {
        if (!ev->state) {
            suppressed->active = false;
        }
        return ZMK_EV_EVENT_CAPTURED;
    }
    for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
        struct active_smt_tog *smt_tog = &active_smt_togs[i];
        if (!smt_tog->is_active) {
            continue;
        }
        const struct zmk_behavior_binding *position_binding =
            find_position_behavior(smt_tog->config, ev->position);
        if (position_binding != NULL) {
            struct zmk_behavior_binding_event event = {
                .layer = smt_tog->layer,
                .position = ev->position,
                .timestamp = ev->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                .source = ev->source,
#endif
            };
            zmk_behavior_invoke_binding(position_binding, event, ev->state);
            return ZMK_EV_EVENT_CAPTURED;
        }
        if (is_position_ignored(smt_tog, ev->position)) {
            continue;
        }
        if (!ev->state) {
            continue;
        }
        LOG_DBG("smart toggle at pos %d interrupted by pos %d", smt_tog->position, ev->position);
        suppress_position_event(ev);
        queue_smt_tog_release(smt_tog);
        return ZMK_EV_EVENT_CAPTURED;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int smt_tog_layer_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || ev->state) { // ignore layer "on" events
        return ZMK_EV_EVENT_BUBBLE;
    }
    for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
        struct active_smt_tog *smt_tog = &active_smt_togs[i];
        if (!smt_tog->is_active || ev->layer != smt_tog->layer) {
            continue;
        }
        LOG_DBG("smart toggle at pos %d ending, layer %d deactivated", smt_tog->position, ev->layer);
        queue_smt_tog_release(smt_tog);
        return ZMK_EV_EVENT_BUBBLE;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int smt_tog_listener(const zmk_event_t *eh) {
    if (as_zmk_position_state_changed(eh) != NULL) {
        return smt_tog_position_state_changed_listener(eh);
    }
    if (as_zmk_layer_state_changed(eh) != NULL) {
        return smt_tog_layer_state_changed_listener(eh);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_smt_tog, smt_tog_listener);
ZMK_SUBSCRIPTION(behavior_smt_tog, zmk_position_state_changed);
ZMK_SUBSCRIPTION(behavior_smt_tog, zmk_layer_state_changed);

static const struct behavior_driver_api behavior_smt_tog_driver_api = {
    .binding_pressed = on_smt_tog_binding_pressed,
    .binding_released = on_smt_tog_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define SMT_TOG_VALIDATE_POSITION_CONFIG(inst)                                                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_bindings),                                    \
                (BUILD_ASSERT(DT_INST_NODE_HAS_PROP(inst, position_binding_behaviors),             \
                              "position-binding-behaviors must be defined when position-bindings is used"); \
                 BUILD_ASSERT(DT_INST_PROP_LEN(inst, position_bindings) ==                         \
                              DT_INST_PROP_LEN(inst, position_binding_behaviors),                  \
                              "position-binding-behaviors length must match position-bindings length");), \
                ())

#define SMT_TOG_BINDING_PARAM(inst, idx, cell)                                                     \
    COND_CODE_1(DT_PHA_HAS_CELL(DT_DRV_INST(inst), position_binding_behaviors, cell),              \
                (DT_PHA_BY_IDX(DT_DRV_INST(inst), position_binding_behaviors, idx, cell)), (0))

#define SMT_TOG_POSITION_BEHAVIOR_ENTRY(idx, inst)                                                 \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_GET(DT_INST_PHANDLE_BY_IDX(inst, position_binding_behaviors, idx)), \
        .param1 = SMT_TOG_BINDING_PARAM(inst, idx, param1),                                        \
        .param2 = SMT_TOG_BINDING_PARAM(inst, idx, param2),                                        \
    }

#define SMT_TOG_DEFINE_POSITION_BEHAVIORS(inst)                                                    \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_binding_behaviors),                           \
                (static const struct zmk_behavior_binding                                         \
                     behavior_smt_tog_position_behavior_bindings_##inst[] = {                      \
                         LISTIFY(DT_INST_PROP_LEN(inst, position_binding_behaviors),               \
                                 SMT_TOG_POSITION_BEHAVIOR_ENTRY, (,), inst)};),                   \
                ())

#define SMT_TOG_DEFINE_POSITION_BINDINGS(inst)                                                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_bindings),                                    \
                (static const uint32_t behavior_smt_tog_positions_##inst[] =                       \
                     DT_INST_PROP(inst, position_bindings);),                                      \
                ())

#define SMT_TOG_DEFINE_IGNORED_POSITIONS(inst)                                                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, ignored_key_positions),                                \
                (static const uint8_t behavior_smt_tog_ignored_positions_##inst[] =                \
                     DT_INST_PROP(inst, ignored_key_positions);),                                  \
                ())

#define SMT_TOG_CONFIG_IGNORED_POSITIONS(inst)                                                     \
    .ignored_key_positions =                                                                       \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, ignored_key_positions),                            \
                    (behavior_smt_tog_ignored_positions_##inst), (NULL)),                          \
    .ignored_key_positions_len =                                                                   \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, ignored_key_positions),                            \
                    (DT_INST_PROP_LEN(inst, ignored_key_positions)), (0))

#define SMT_TOG_CONFIG_POSITION_DATA(inst)                                                         \
    .position_bindings =                                                                           \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_bindings),                                \
                    (behavior_smt_tog_positions_##inst), (NULL)),                                  \
    .position_bindings_len =                                                                       \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_bindings),                                \
                    (DT_INST_PROP_LEN(inst, position_bindings)), (0)),                              \
    .position_binding_behaviors =                                                                  \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_binding_behaviors),                       \
                    (behavior_smt_tog_position_behavior_bindings_##inst), (NULL)),                 \
    .position_binding_behaviors_len =                                                              \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, position_binding_behaviors),                       \
                    (DT_INST_PROP_LEN(inst, position_binding_behaviors)), (0))

#define ST_INST(n)                                                                                 \
    SMT_TOG_DEFINE_IGNORED_POSITIONS(n)                                                            \
    SMT_TOG_VALIDATE_POSITION_CONFIG(n)                                                            \
    SMT_TOG_DEFINE_POSITION_BEHAVIORS(n)                                                           \
    SMT_TOG_DEFINE_POSITION_BINDINGS(n)                                                            \
    static const struct behavior_smt_tog_config behavior_smt_tog_config_##n = {                    \
        SMT_TOG_CONFIG_IGNORED_POSITIONS(n),                                                       \
        SMT_TOG_CONFIG_POSITION_DATA(n),                                                           \
        .tog_behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                             \
        .continue_behavior = ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n))};                       \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_smt_tog_init, NULL, NULL, &behavior_smt_tog_config_##n,    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &behavior_smt_tog_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ST_INST)
