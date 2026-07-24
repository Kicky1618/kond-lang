# Kond

`spec_v0.2/` の Draft 0.2 を基準にした、C++17製のKond処理系です。外部ランタイムに依存しない単一バイナリのASTインタプリタで、証明付き高速化だけを意味論を変えずに適用します。

```text
Any value
  ├─ check / prove ──> Condition<P> = Holds | Fails
  ├─ where invariant ──> transactional mutation
  ├─ move / borrow ──> deterministic ownership certificate
  └─ flow label ──> explicit / strict IFC
```

## ビルド

```sh
make
make test
```

既定のリリースビルドは `-O3 -flto -DNDEBUG` です。実行するCPU専用に最適化する場合は次を使えます。

```sh
make native
```

CMakeにも対応しています。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## ドキュメントサイト

Next.js + React + Turbopack + UnoCSS のサイトを同じリポジトリ内で管理しています。

```sh
npm install
npm run dev
```

トップページは `http://localhost:3000/`、仕様書は `http://localhost:3000/docs` から確認できます。
本番用の確認は `npm run build && npm run start` です。

`-DKOND_NATIVE_OPTIMIZATIONS=ON` を追加すると `-march=native` を有効にします。

## LSP

Kond の言語サーバーは stdio の JSON-RPC で起動できます。

```sh
./kond lsp
```

エディタ側では実行ファイルを `kond lsp` として `.kd` に接続してください。
`initialize`、ドキュメント同期、構文/所有権診断、補完、ホバー、定義、参照、
ドキュメントシンボル、ワークスペースシンボルに対応しています。ワークスペースの
未オープンな `.kd` は `rootUri` から必要に応じて読み込みます。

## 実行

```sh
./kond run examples/core.kd
./kond run examples/ownership.kd --entry demo
./kond run examples/order_processing.kd
./kond check spec/examples/server.kd
./kond run examples/v02_strict_ifc.kd --ifc strict

# route 宣言を標準 HTTP サーバーとして起動
./kond serve spec/examples/server.kd --bind 127.0.0.1 --port 8080
```

`run` は省略できます。安全モードは3種類です。

- `safe`: 静的に証明できない決定可能な条件を実行時に検査
- `verified`: 必要な静的証明がない場合に拒否
- `unsafe`: `assume`、`declassify`、`endorse` を許す明示的な信頼境界

### C FFI

POSIX環境では、C ABIの共有ライブラリを `extern fn` で呼び出せます。
FFIの宣言は `Int` (= `int64_t`)、`Float` (= `double`)、`Bool`
(= `int64_t` の0/1)、`String` (= `const char *`) と `Void` に限定しています。
共有ライブラリは実行時に `libffi` と `dlopen` でロードされ、`String` の戻り値は
Kond側へコピーされます。

```kond
unsafe extern fn add(left: Int, right: Int) -> Int
    from "./libmath.so" as "my_add";

fn main() {
    unsafe {
        print(add(20, 22))
    }
}
```

外部関数の呼び出しには `unsafe { ... }` または `--mode unsafe` が必要です。
共有ライブラリ側の関数は `extern "C"` で公開してください。FFIは現在のLLVM
JITでは未対応のため、インタプリタで実行します。ポインタ、構造体、配列、コールバック、
所有権付きの外部リソースはまだABIに含めません。

新しい最小プロジェクトは `new` で生成できます。

```sh
./kond new hello
cd hello
../kond check main.kd
../kond run main.kd
```

`new` は `kond.json`、`main.kd`、`README.md` を生成します。既存の非空
ディレクトリは上書きしません。

## パッケージ管理

`kond.json` はパッケージ名・バージョン・エントリポイント・依存関係を
記述します。現在の依存関係はローカルパスに対応しています。FFI用の
共有ライブラリは、パッケージ相対パスの `native` 配列に列挙します。

```json
{
  "name": "app",
  "version": "0.1.0",
  "entry": "main.kd",
  "native": ["native/libmath.so"],
  "dependencies": {
    "greeting": { "path": "../greeting", "version": "0.1.0" }
  }
}
```

```sh
./kond add ../greeting --project app
./kond install app
./kond list app
./kond run app
./kond remove greeting --project app
```

`install` は依存関係を検査して `kond.lock` に解決結果を保存します。
パッケージの `library` に指定した `.kd` は `fn`、`condition`、`extern fn` のみを
公開するソースライブラリとして、プロジェクトディレクトリを指定した
`run` / `check` から自動的に読み込まれます。レジストリ経由で取得した
パッケージも同じソースライブラリ方式で実行されます。`native` に列挙した
共有ライブラリは registry bundle に含まれ、`fetch` 後も `from "native/..."`
の相対パスでロードできます。これはOSのC ABIであり、安定した
Kond独自のバイナリパッケージABIではありません。

### ローカルレジストリ

組み込みの逐次HTTPレジストリで、KondのソースパッケージとPOSIX C FFI用の
共有ライブラリをホストできます。
storageディレクトリはパッケージbundleを永続化するだけのローカルストレージです。
公開・取得コマンドの既定レジストリは `http://kond.j9.si` です。
`--registry URL` または `KOND_REGISTRY` 環境変数で変更できます。

```sh
# 開発用レジストリを起動（127.0.0.1:8787）
./kond registry .kond-registry --bind 127.0.0.1 --port 8787

# 依存関係を持たないソースパッケージを公開
./kond publish examples/package_library --registry http://127.0.0.1:8787
./kond publish examples/score_package --registry http://127.0.0.1:8787

# native配列で列挙した共有ライブラリも同梱して公開
cc -shared -fPIC examples/ffi_package/native/ffi_math.c \
  -o examples/ffi_package/native/libffi_math.so
./kond publish examples/ffi_package --registry http://127.0.0.1:8787

# 別プロジェクトへ取得。vendor/へ展開し、kond.json/kond.lockも更新
./kond new consumer
./kond fetch greeting 0.1.0 --registry http://127.0.0.1:8787 --project consumer
./kond run consumer
```

既定レジストリを使う場合は `--registry` を省略できます。
ローカルレジストリを既定値として試す場合は、次のようにします。

```sh
KOND_REGISTRY=http://127.0.0.1:8787 ./kond publish examples/package_library
KOND_REGISTRY=http://127.0.0.1:8787 ./kond fetch greeting 0.1.0 --project consumer
```

レジストリは `GET /healthz`、`GET /index.json`、
`GET /packages/<name>/<version>` と、同じpackage pathへの `POST` / `PUT` を提供します。
`--once` を付けると1リクエストで終了するため、動作確認にも使えます。
この実装は開発・社内ネットワーク向けのHTTPサーバーで、認証、TLS、署名検証、
依存パッケージのレジストリ解決は行いません。公開bundleは現在、依存関係なしの
Kondパッケージに限定されます。native artifact はパッケージ相対パスに限られ、
registry bundle内ではbase64化した `binary` objectとして転送されます。FFIの
利用可能ABIはPOSIXの限定C ABIで、対象OS・CPU向けに共有ライブラリをビルドして
公開する必要があります。`kond.j9.si` を公開運用する際は、DNSとTLS終端を別途
用意してください。現行クライアントのregistry通信はHTTPのみです。

診断用オプション:

```sh
./kond run examples/v02_math_opt.kd --explain-optimizations
./kond run examples/optimization_library.kd --opt-lib std/optimization.kd --explain-optimizations
./kond run examples/source_library_consumer.kd --lib examples/source_library.kd
./kond run examples/score_library_consumer.kd --lib examples/score_library.kd
./kond run examples/ownership.kd --entry demo --trace-ownership

# LLVM ORC JIT（LLVM/llvm-config がある環境で有効）
./kond run examples/jit_arithmetic.kd --jit
./kond run examples/jit_arithmetic.kd --jit --dump-llvm
```

## Draft 0.2対応範囲

- 値: `Null`、`Int64`、`Float64`、`Bool`、`String`、`List`、`Object`、`HttpRequest`、`HttpResponse`、関数、借用参照
- 条件: `is`、比較、`and/or/not`、`has` shape schema、名前付き条件、`all`、ラムダ
- 実行時条件値: `Condition<P> = Holds(Proof<P>) | Fails(Proof<not P>)`
- 値バージョンに束縛された条件証拠と、更新後のstale-proof拒否
- `check`、`prove`、`require`、`assume`、`safe/verified/unsafe`
- `if`、`while invariant`、`for item in list`、`match`、早期`return`
- `where`不変条件付きスロット、`= += -= *= /=`、任意placeへのtransactional `update`
- `move`、共有借用、可変借用、字句スコープでの借用復元
- 専用の決定的所有権プリフライトと E21xx/E22xx 診断
- 所有権遷移証明トレース: `Own -> Moved`、`Own -> SharedBorrow`、`BorrowEnd -> Own(vN)`
- 関数の `requires`、`ensures`、`flow result <- ...`
- `Public`、`Untrusted`、`Secret`、`Personal` のflow join
- explicit IFCと、分岐のpc-labelも伝播するstrict IFC
- `html"..."` の文脈エスケープ、`sql"..."` のパラメータ化
- taintを消さないsanitizerと、unsafe境界限定のdeclassification/endorsement
- `route` 宣言およびDraft 0.2付属サンプルの構文
- 標準 HTTP サーバー (`serve`) と、`req.json()` を含むHTTP入力境界
- オーバーフロー検査付き `Int64` 演算
- `std.core`、`std.math`、`std.string`、`std.list`、`std.map`、`std.json`、`std.url`、`std.html`、`std.security`、`std.http`、`std.io`、`std.pred`、`std.opt`
- `rewrite exact|real|approx|heuristic` と `--opt-lib` によるユーザー最適化ライブラリ
- `--lib` による明示的なソースライブラリ（`fn` と `condition` のみ）
- C ABI共有ライブラリを呼び出す `unsafe extern fn` FFI（POSIX + libffi）

`spec/examples/math_opt.kd`、`ownership.kd`、`server.kd` はすべて `kond check` を通過します。所有権違反は一般の条件ソルバへ渡さず、専用チェッカーで決定的に判定します。

実用フローの例として [`examples/order_processing.kd`](examples/order_processing.kd) では、注文の形状・明細を検証してから、借用、リスト更新、合計計算、SQL保存、HTML表示までを行います。`untrusted("<Ada>")` はHTML出力時に `&lt;Ada&gt;` としてエスケープされ、SQLの補間値はパラメータ化されます。

## 高速化

PIRの証明クラスを混同せず、現在は以下の `ExactEq` 高速化を実装しています。

- 不変条件付き単調整数ループの閉形式化
- `(x / d) * d -> x`（`d > 0` かつ `x % d == 0` の証明がある場合）
- `abs(x) -> x`（`x >= 0` の証明がある場合）
- `x & (2^n - 1) -> x`（値域証明がある場合）
- Int64のzero/one/self演算、bitwise identity、二重否定
- 純粋な式と標準ライブラリAPIのconstant folding
- `range`、`repeat`、`concat`、`reverse`、`sort`、`zip`、`slice` のlength計算で中間Listを省略
- `std.string.repeat` のlength計算で中間Stringを省略

100万回の整数更新を行う `bench/numeric_loop.kd` は、この環境で通常解釈の約4.15秒から閉形式実行の約0.001〜0.002秒になりました。環境依存の参考値ですが、反復回数に比例していた処理を定数時間へ変換しています。

```sh
./kond run bench/numeric_loop.kd --explain-optimizations
./kond run bench/collection_length.kd --explain-optimizations
```

ユーザー定義の厳密な整数規則は、30規則を収録した `std/optimization.kd` のようなライブラリとして
明示的に読み込めます。`exact` 規則は現在の証明環境で条件が証明され、対象式が
副作用なしの場合だけ適用されます。`real`、`approx`、`heuristic` は候補として登録
されますが、Draft 0.2 の実行時に `ExactEq` へ昇格されません。

通常のソースライブラリは `--lib FILE` で読み込みます。ライブラリの
`fn` と `condition` は本体へ統合されますが、読み込み時にトップレベル文は
実行されません。`route` と `rewrite` は専用の実行境界を持つため、通常の
ソースライブラリには置けません。現在は名前空間やバージョン管理を持たない
明示ロード方式であり、独立パッケージABIではありません。`extern fn` の共有ライブラリは
実行時に解決されます。

標準ライブラリの一覧と意味論は [`spec/docs/15-standard-library.md`](spec/docs/15-standard-library.md)、
サンプルは [`examples/std_library.kd`](examples/std_library.kd) を参照してください。

浮動小数点については `RealEq` を `ExactEq` に昇格しません。許容誤差を伴う変換も、`ApproxEq(metric,bound,domain)` の証明器が未実装のため適用しません。

## 現在の境界

これはDraft 0.2の実行可能なコア実装です。完全なCIR/FIR/PIRシリアライズ、汎用SMT連携、ユーザー定義linear protocol automata、完全な部分move解析、DB接続、WASMバックエンド、安定したKond独自のバイナリパッケージABIは未実装です。FFIは限定されたC ABIの共有ライブラリ呼び出しまでで、native artifactのregistry公開はPOSIX向けです。`kond registry` は開発用の逐次HTTPサーバーであり、本番向けの認証・TLS・署名検証は別途必要です。`serve` は標準C++とOSのソケットAPIだけで実装した逐次HTTPサーバーで、`route` を実際のHTTP入力境界として実行します。データベース接続は行わず、`database.query` はSQL sink の契約を検査する標準スタブです。

LLVM が検出されたビルドでは `--jit` が整数値サブセットを LLVM IR に lowering し、LLVM ORC の `LLJIT` でネイティブコードとして実行します。対応範囲は Int64 の算術／bitwise 演算、条件、`if`、`while`、関数呼び出し、`print`、`requires`／`ensures`／`where` の実行時ガードです。List、Object、HTTP 値、borrow／move、動的な標準ライブラリ API はインタプリタで実行してください。`--mode verified` の静的証明は JIT backend では未対応なので、JIT では拒否します。`--dump-llvm` で生成 IR を確認できます。
# kond-lang
