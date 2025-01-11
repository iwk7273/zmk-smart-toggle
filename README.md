# Smart Toggle Behavior for ZMK

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

> [!IMPORTANT]
> If you place this behavior on a combo, make sure to include the combo `key-positions` in `ignored-key-positions`.

## Installation

See [ZMK modules documentation](https://zmk.dev/docs/features/modules#building-with-modules) for adding this module to your ZMK build.

## Acknowledgements

This implementation is a simplified version of the tri-state behavior implemented by Nick Conway in [ZMK PR #1366](https://github.com/zmkfirmware/zmk/pull/1366). I used the [module-ified version](https://github.com/dhruvinsh/zmk-tri-state) by Dhruvin Shah as well.

The reason I chose to not use it as is is so I can simplify and clean it up the way I prefer it, and thus be able to maintain easier going forward.

## Related

- Tri-state behavior ([PR](https://github.com/zmkfirmware/zmk/pull/1366), [module](https://github.com/dhruvinsh/zmk-tri-state))
- [Auto layer behavior](https://github.com/urob/zmk-auto-layer)
