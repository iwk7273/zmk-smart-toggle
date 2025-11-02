# ZMK向けスマートトグル動作

[English README](README.md)

このリポジトリは、ZMK に「スマートトグル」キーを追加するカスタム動作[モジュール](https://zmk.dev/docs/features/modules)です。

スマートトグル動作は [key toggle](https://zmk.dev/docs/keymaps/behaviors/key-toggle) に似ていますが、同じキーで解除する代わりに、
配置されたレイヤーが無効になった場合、または自身（と任意で指定したポジション）以外のキーが押された場合に自動で解除されます。

簡単な例として、以下のように `&hold_ctrl` を定義しておくと、割り当てたレイヤーがアクティブな間は <kbd>Ctrl</kbd> が押下されたままになります。

```dts
    behaviors {
        hold_ctrl: hold_ctrl {
            compatible = "zmk,behavior-smart-toggle";
            #binding-cells = <0>;
            bindings = <&kp LCTRL>, <&none>;
        };
    };
```

さらに、トグル中に同じキーを押した際に別の動作を送出することもできます。
たとえば、最初のタップで <kbd>Alt</kbd> + <kbd>Tab</kbd> を送り、以降のタップは <kbd>Tab</kbd> として扱い、
他のキーを押すか `layer_nav` レイヤーを無効化したタイミングで <kbd>Alt</kbd> を離す、といったウィンドウスイッチャー（Alt-Tab/Cmd-Tab）を 1 キーで構成できます。

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

`ignored-key-positions = <1>;` としておくと、そのレイヤーの 2 番目のポジションでもトグルが解除されなくなるため、
<kbd>Shift</kbd> + <kbd>Tab</kbd> を使った逆方向の移動も可能になります。

（この動作は通称 ["swapper"](https://github.com/callum-oakley/qmk_firmware/tree/master/users/callum#swapper) と呼ばれます。）

トグル中のみ有効にしたい別のバインディングを設定することもできます。
`position-bindings` にリマップしたいポジションを列挙し、同じ順序で `position-binding-behaviors` に追加の動作を記述してください。

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

トグルが有効な間は、ポジション 11 を押すと <kbd>Left</kbd>、ポジション 12 を押すと <kbd>Right</kbd> が送られます。
これらのリマップされたポジションは元のキー入力を抑止し、トグル状態も維持します。
`ignored-key-positions` に含まれないその他のキーを押すとトグルは解除され、そのキー入力は送出されません。

> [!IMPORTANT]
> この動作をコンボに割り当てる場合は、コンボに含まれる `key-positions` を `ignored-key-positions` に追加してください。

## 元のリポジトリからの変更点

- トグル中のみ別動作を実行できる `position-bindings` / `position-binding-behaviors` を追加。
- `bindings` はトグル開始と継続の 2 つだけを保持するよう簡素化。
- Devicetree バインディングを更新し、ポジションと動作の 1:1 対応をビルド時に検証。
- `ignored-key-positions` 以外のキーを押した場合は、トグルを解除して元のキー入力を抑止するように変更。

## インストール

ZMK ビルドへモジュールを追加する手順は [ZMK Modules ドキュメント](https://zmk.dev/docs/features/modules#building-with-modules) を参照してください。

## 関連リソース

- トライステート動作（[PR](https://github.com/zmkfirmware/zmk/pull/1366)、[モジュール](https://github.com/dhruvinsh/zmk-tri-state)）
- [Auto layer behavior](https://github.com/urob/zmk-auto-layer)
- [Smart Toggle Behavior (Original Repository)](https://github.com/dhruvinsh/zmk-smart-toggle)
- [Nick Conway 氏によるトライステート実装への謝辞](https://github.com/zmkfirmware/zmk/pull/1366) と [Dhruvin Shah 氏によるモジュール化版](https://github.com/dhruvinsh/zmk-tri-state)
