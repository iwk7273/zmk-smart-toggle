#define DT_DRV_COMPAT zmk_behavior_smart_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
    struct zmk_behavior_binding tog_behavior;
    struct zmk_behavior_binding continue_behavior;
    uint8_t ignored_key_positions[];
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
    if (smt_tog->position == position) {
        return true;
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
    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    for (int i = 0; i < ZMK_BHV_MAX_ACTIVE_SMT_TOGS; i++) {
        struct active_smt_tog *smt_tog = &active_smt_togs[i];
        if (!smt_tog->is_active || is_position_ignored(smt_tog, ev->position)) {
            continue;
        }
        LOG_DBG("smart toggle at pos %d interrupted by pos %d", smt_tog->position, ev->position);
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

#define ST_INST(n)                                                                                 \
    static struct behavior_smt_tog_config behavior_smt_tog_config_##n = {                          \
        .ignored_key_positions = DT_INST_PROP(n, ignored_key_positions),                           \
        .ignored_key_positions_len = DT_INST_PROP_LEN(n, ignored_key_positions),                   \
        .tog_behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                             \
        .continue_behavior = ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n))};                       \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_smt_tog_init, NULL, NULL, &behavior_smt_tog_config_##n,    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &behavior_smt_tog_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ST_INST)
