# Smart Toggle Behavior for ZMK

[日本語版 README](README.ja.md)

This is a custom behavior [module](https://zmk.dev/docs/features/modules) that implements a "smart toggle" key.

The smart toggle behavior is like the [key toggle behavior](https://zmk.dev/docs/keymaps/behaviors/key-toggle),
except instead of pressing the same key to toggle it off,
it turns itself off when layer it is on is deactivated or a key besides itself (or ones in an optional positions list) is pressed.

As a simple example, tapping the `&hold_ctrl` behavior as defined below will keep <kbd>Ctrl</kbd> held while the layer it was placed in is active:

```dts
    behaviors {
        hold_ctrl: hold_ctrl {
            compatible = "zmk,behavior-smart-toggle";
            #binding-cells = <0>;
            bindings = <&kp LCTRL>, <&none>;
        };
    };
```

In addition, pressing the same key while the toggle is active can send a different behavior.
For example, this allows you to define a single key window switcher (alt-tab/cmd-tab) where the first tap
presses <kbd>Alt</kbd> and taps <kbd>Tab</kbd>, consecutive taps are interpreted as <kbd>Tab</kbd>, and pressing another key or deactivating
the `layer_nav` layer releases <kbd>Alt</kbd>:

```dts
/ {
    behaviors {
        swapper: swapper {
            compatible = "zmk,behavior-smart-toggle";
            #binding-cells = <0>;
            bindings = <&kp LALT>, <&kp TAB>;
            ignored-key-positions = <1>;
        };

        /* ...other behaviors... */
    };

    keymap {
        compatible = "zmk,keymap";

        /* ...other layers... */

        layer_nav {
            bindings = <&swapper &kp LS(TAB) /* ... */>;
        };
    };
};
```

`ignored-key-positions = <1>;` specifies that the second position on the layer will also not toggle off <kbd>Alt</kbd>,
so you can use it to go backwards in the window list using <kbd>Shift</kbd>+<kbd>Tab</kbd>.

(This behavior is colloquially known as the ["swapper"](https://github.com/callum-oakley/qmk_firmware/tree/master/users/callum#swapper).)

You can also provide alternate bindings that are only active while the toggle is on.
List the positions that should be remapped in `position-bindings`, and define the extra behaviors in
`position-binding-behaviors` in the same order:

```dts
        swapper: swapper {
            compatible = "zmk,behavior-smart-toggle";
            #binding-cells = <0>;
            bindings = <&kp LALT>, <&kp TAB>;
            ignored-key-positions = <1>;
            position-bindings = <11 12>;
            position-binding-behaviors = <&kp LEFT>, <&kp RIGHT>;
        };
```

While the toggle is active, pressing position 11 sends <kbd>Left</kbd> and position 12 sends <kbd>Right</kbd>.
These remapped positions automatically keep the toggle active and suppress the original key behavior.
Pressing any other non-ignored position cancels the toggle and suppresses that key press.

> [!IMPORTANT]
> If you place this behavior on a combo, make sure to include the combo `key-positions` in `ignored-key-positions`.

## Changes From The Original Repository

- Added `position-bindings` / `position-binding-behaviors` so specific key positions can trigger alternate behaviors only while the toggle is active.
- Simplified the configuration so `bindings` only contains the toggle-on and continue behaviors.
- Updated the devicetree binding to document the new properties and enforce one-to-one pairing between positions and behaviors at build time.
- Non-ignored positions now cancel the toggle without sending their original key output.

## Installation

See [ZMK modules documentation](https://zmk.dev/docs/features/modules#building-with-modules) for adding this module to your ZMK build.

## Related

- Tri-state behavior ([PR](https://github.com/zmkfirmware/zmk/pull/1366), [module](https://github.com/dhruvinsh/zmk-tri-state))
- [Auto layer behavior](https://github.com/urob/zmk-auto-layer)
- [Smart Toggle Behavior (Original Repository)](https://github.com/dhruvinsh/zmk-smart-toggle)
- Acknowledgements to [Nick Conway](https://github.com/zmkfirmware/zmk/pull/1366) for the tri-state concept and [Dhruvin Shah](https://github.com/dhruvinsh/zmk-tri-state) for the modularized version
