# Kond 標準ライブラリ

標準ライブラリは、処理系に組み込まれた名前空間APIとして提供されます。全プログラムから `std.*` で利用できます。

```kd
let xs = std.core.range(0, 10)
let evens = std.list.filter(xs, x => std.pred.even(x))
let encoded = std.url.encode_component("a value")
```

標準APIの仕様は [`spec/docs/15-standard-library.md`](../spec/docs/15-standard-library.md) にあります。

`optimization.kd` は別途読み込める証明付き最適化ライブラリです。
現在はゼロ/単位元、bitwise、self演算、除算・剰余の安全な恒等変形を含む
30個の `ExactEq` 規則を収録しています。

```sh
kond run program.kd --opt-lib std/optimization.kd
```

`exact` 規則だけが実行時の書き換え対象になります。`real`、`approx`、`heuristic` は構文上登録できますが、IEEE/実行時意味論の証明がない限り実行されません。
