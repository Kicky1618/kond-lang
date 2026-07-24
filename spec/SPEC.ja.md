# Kond 言語仕様書 — Draft 0.2

**状態:** 実験的設計仕様  
**言語名:** `Kond` は仮称であり、仕様上固定されていない。

## 0. 設計理念

Kond は、従来の静的型付け・動的型付けのどちらとも異なるモデルを採用する。

すべての実行時値は基本的に `Any` である。  
値そのものに固定的な型を貼る代わりに、その値について現在証明できている事実を `Condition<P>` として蓄積する。

```text
値の世界          Any
知識の世界        Condition<P>
線形資源の世界    LinearCondition<L>
可変変数          Invariant<P> を保存するスロット
操作              必要条件の証明 + 線形条件の消費 + 新しい事実の生成
```

Kond の中心原則は次である。

> 「この値の型は何か」ではなく、「この操作を安全に行うために必要な条件を、現在の事実から証明できるか」を問う。

この一つの原理で、次を統一することを目的とする。

- 型絞り込み
- 範囲検査
- 配列境界安全性
- null 安全性
- 可変性
- 所有権と借用
- typestate
- capability security
- Web 入力検証
- taint tracking
- 認証・認可
- SQL/XSS/path traversal 対策
- JIT 特殊化
- 数学的最適化
- コンパイラ診断

---

## 0A. Draft 0.2 で確定した意味論上の境界

### runtime `Condition<P>`

実行時判定可能な条件 `P` は概念上、

```text
Condition<P> = Holds(Proof<P>) | Fails(Proof<not P>)
```

を返す。「bool が存在しない」とは CPU に1bitが存在しないという意味ではなく、**述語結果を出自の命題を失った裸の bool として公開しない**という意味である。proof payload は最適化時に erase してよい。

`print(condition)` の標準表示は `true` / `false`。異なる命題の condition を混在保存する場合は `exists P. Condition<P>` の existential package を用いる。`and/or/not` は命題 index と証拠を保存して合成する。

### taint / information flow

`Untrusted(x)` は普通の述語を書いただけでは伝播しない。専用 Flow Domain が全 primitive/builtin の flow summary を持ち、デフォルトでは `label(result)=join(label(inputs...))` とする。Strict IFC では `pc label` も join して implicit flow を追跡する。

sanitizer は `Untrusted` を万能に消去せず、`HtmlText`、`SqlParameter`、`UrlComponent` など文脈固有の安全条件を生成する。

### ownership checker

ownership は意味論上 `LinearCondition` として統一するが、通常の borrow legality は dedicated deterministic checker で判定する。一般 solver の proof budget や timeout に borrow 成否を依存させない。checker は ownership/capability facts と linear certificate を出力し、kernel が検証する。

### exact / approximate optimization

PIR は `ExactEq`、`RealEq`、`ApproxEq(metric,bound,domain)`、`HeuristicImprovement` を区別する。`RealEq` を floating-point の `ExactEq` として使うことは禁止する。`Orthogonal<Real>(A)` だけでは IEEE 演算の `transpose(A)*A` を厳密な Identity に置換できない。

---

## 1. 値モデル

### 1.1 すべての値は `Any`

Kond において、変数の基本的な値領域は `Any` である。

```kond
let x = input()
```

この時点で、コンパイラは原則として `x` の具体的性質を知らない。

外部入力であれば、標準ライブラリは通常さらに次の事実を与える。

```text
Untrusted(x)
```

`Int`、`String`、`List`、`User` などは通常の意味での「型」ではなく、条件述語である。

```text
Int(x)
String(x)
List(x)
User(x)
```

### 1.2 条件によって知識を追加する

```kond
check x is Int
```

成功後の事実環境には、

```text
Int(x)
```

が追加される。

さらに、

```kond
check 0 <= x < 100
```

を通過すれば、

```text
Int(x)
0 <= x
x < 100
```

が現在の値バージョンについて証明済みとなる。

---

## 2. `bool` は存在しない

### 2.1 論理式は `Condition<P>` を返す

Kond にはプリミティブな `bool` 型を置かない。

```kond
x > 0
```

は概念上、

```text
Condition<x > 0>
```

を生成する。

コンパイラ内部では、条件の証明状態を次の三つに区別する。

```text
Proven(P)   P が証明済み
Refuted(P)  not P が証明済み
Unknown(P)  どちらも証明できていない
```

`Unknown` は三値論理の第三の真理値ではない。  
単に「コンパイラがまだ決定できていない」という解析状態である。

実行時に決定可能な条件を分岐に使う場合は、実行時評価により `P` または `not P` のどちらかが成立する。

### 2.2 `if` は条件を分解する

```kond
if x is Int {
    // Int(x)
} else {
    // not Int(x)
}
```

`if` は bool を読む構文ではなく、`Condition<P>` を分岐させ、各枝に証明を導入する構文である。

### 2.3 条件値は値バージョンに束縛される

```kond
let x where self is Int = 10
let positive = x > 0
x = -10
```

内部的には、

```text
x0 = 10
positive : Condition<x0 > 0>
x1 = -10
```

となる。

`positive` は `x1` には適用できない。

この SSA 的な値バージョン規則によって、「昔チェックした条件を変更後の値に誤用する」バグを防ぐ。

---

## 3. 条件の半順序

条件は「許可する値集合」として解釈する。

```text
P <= Q
```

を、

```text
forall x. P(x) => Q(x)
```

と定義する。

このとき `P` は `Q` より強い条件であり、許容集合が小さい。

```text
Int(x) and 0 <= x < 100
    <= Int(x) and x >= 0
    <= Int(x)
    <= AnyCondition(x)
```

条件比較は全順序ではない。

```text
Int(x)
String(x)
```

は一般に比較不能である。

比較結果は次を持つ。

```text
Equivalent
Stronger
Weaker
Incomparable
Unknown
```

`Unknown` は証明器が決定できなかったことを意味する。

---

## 4. 条件の宣言と合成

### 4.1 条件宣言

```kond
condition User(u) =
    u has { name, age } and
    u.name is String and
    u.age is Int and
    0 <= u.age < 150
```

これは nominal type の宣言ではなく、述語の定義である。

```kond
condition Adult(u) =
    User(u) and u.age >= 18

condition Admin(u) =
    User(u) and "admin" in u.permissions
```

一つの値が複数条件を同時に満たしてよい。

### 4.2 論理演算

```text
P and Q
P or Q
not P
```

は bool 演算ではなく、条件を構成する演算である。

### 4.3 恒真・恒偽

プリミティブ bool リテラルの代わりに、

```text
always
never
```

を用いる。

---

## 5. 証明操作

### 5.1 `prove`

```kond
prove P
```

`P` が静的に証明可能であることを要求する。

実行時チェックを挿入してはならない。

### 5.2 `check`

```kond
check P
```

静的に証明できればチェックを消去する。  
証明できず、かつ実行時に決定可能ならランタイムチェックを生成する。

成功後は `P` が事実環境に追加される。

### 5.3 `require`

```kond
require P
```

外部入力境界や API 契約で利用する高水準チェック。

Web フレームワークでは失敗を HTTP 400 等へ変換できる。

### 5.4 `assume`

```kond
unsafe {
    assume P
}
```

証明せずに条件を導入する。

`assume` は明示的な信頼境界であり、通常コードでは使用できない。

---

## 6. 条件保存変数

### 6.1 `mut` を宣言しない

普通の `let` は不変である。

```kond
let x = 10
x = 20
// error
```

条件付きで宣言すると、条件を保存する範囲で可変になる。

```kond
let age where self is Int and 0 <= self < 150 = 16

age = 20
age = 149
```

次は拒否される。

```kond
age = -1
age = 200
age = "old"
```

可変性は真偽値ではなく、

```text
Assignable(age) =
    { v | Int(v) and 0 <= v < 150 }
```

という値集合として定義される。

### 6.2 代入は再証明

```kond
age = candidate
```

は、

```text
Facts(candidate)
    =>
Invariant_age(candidate)
```

を証明する操作である。

Safe モードでは不明ならチェックを挿入できる。

Verified モードでは `Unknown` はコンパイルエラーとなる。

### 6.3 commit-before-check を禁止

代入は必ず、

```text
candidate を計算
↓
条件を検証
↓
成功した場合のみ commit
```

の順で行う。

失敗時に元の値は保持される。

### 6.4 トランザクション更新

複数フィールドにまたがる不変条件には、

```kond
update range {
    self.start = 20
    self.end = 30
}
```

を使用できる。

ブロック中は一時的に条件を破ってよいが、commit 前に全不変条件を再証明・再検査する。

---

## 7. 所有権は線形条件

### 7.1 永続条件と線形条件

通常の数学的事実は自由に再利用できる。

```text
Int(x)
x >= 0
Prime(x)
```

所有権や一回限りの権限は複製できない。

```text
Own(scope, x)
BorrowUnique(r, x)
ValidResetToken(token)
TransactionOpen(tx)
```

これらを `LinearCondition<L>` として扱う。

### 7.2 move

```kond
let y = move x
```

内部的には、

```text
consume Own(scope, x)
produce Own(scope, y)
```

となる。

以後 `x` を所有者として利用できない。

### 7.3 shared borrow

```kond
let r = &x
```

は概念上、

```text
Own(scope, x)
  =>
FrozenOwn(scope, x)
+ BorrowShared(r, x)
```

を生成する。

共有借用中は読み取り可能だが、変更・move は禁止される。

### 7.4 unique borrow

```kond
let r = &mut x
```

は、

```text
Own(scope, x)
  =>
SuspendedOwn(scope, x)
+ BorrowUnique(r, x)
```

と解釈する。

unique borrow は読み書きを許可するが、同時に競合アクセスを許可しない。

### 7.5 capability と統一

```text
Own(_, x)         => CanRead(x), CanWrite(x), CanMove(x), CanDrop(x)
BorrowUnique(_,x) => CanRead(x), CanWrite(x)
BorrowShared(_,x) => CanRead(x)
```

この capability 関係も条件の半順序として扱う。

---

## 8. 関数契約

```kond
fn sqrt(x)
    requires Number(x)
    requires Finite(x)
    requires x >= 0
    ensures Number(result)
    ensures result >= 0
{
    ...
}
```

呼び出し側は `requires` を満たさなければならない。

戻り時には `ensures` が呼び出し側の事実として追加される。

関数契約は、通常の型シグネチャの代わりに「必要な命題」と「生成する命題」を記述する。

---

## 9. 制御フロー推論

### 9.1 早期 return

```kond
if not (x is String) {
    return Error("invalid")
}

// String(x) が成立
```

終了する枝の否定から、継続側を自動的に refinement する。

### 9.2 `match`

```kond
match x {
    when x is Int and x >= 0 => ...
    when x is Int            => ...
    when x is String         => ...
    else                     => ...
}
```

後続枝には、それ以前の条件の否定が自動的に加わる。

2番目の枝は、十分な算術推論があれば、

```text
Int(x) and x < 0
```

まで簡約できる。

### 9.3 コレクション反復

`for` はコレクション式を一度評価し、`List` の要素をソース順の
スナップショットとして反復する。

```kond
let total where self is Int and self >= 0 = 0
for value in std.core.range(1, 6) {
    total += value
}
```

反復変数は不変で、現在の反復の本体内だけで有効である。対象が
`List` でなければ実行時エラーになる。参照実装では反復回数を100万要素
までに制限する。反復元のListを本体で `push` / `pop` しても訪問対象が
変化しないため、反復の挙動は決定的である。

---

## 10. Proof IR アーキテクチャ

コンパイラ内部は少なくとも三層に分ける。

```text
CIR  Condition Intermediate Representation
FIR  Fact Intermediate Representation
PIR  Proof Intermediate Representation
```

### 10.1 CIR

論理条件そのものを正規化して保持する。

概念例:

```rust
enum Condition {
    True,
    False,
    Atom(AtomId),
    And(Vec<ConditionId>),
    Or(Vec<ConditionId>),
}
```

原子条件:

```text
Equal
NotEqual
Less
LessEqual
HasTag
HasField
Predicate
IsFinite
```

### 10.2 FIR

高速な推論用の正規化事実。

推奨ドメイン:

```text
等値クラス
runtime tag
整数/有理数区間
合同式
オブジェクト shape
nullability
typestate
capability
ownership/alias
taint/trust
```

例:

```text
x in [0, 100)
x == y
x == 3 mod 8
tag(x) = Int
fields(x) includes {name, age}
```

### 10.3 PIR

「なぜ P => Q が成立するか」を証明証明書として表現する。

例:

```text
Assumption
Reflexive
Transitive
AndIntroduction
AndElimination
Rewrite
IntervalSubset
TagSubset
CongruenceStep
LinearArithmetic
ApplyLemma
```

### 10.4 小さな trusted kernel

複雑な optimizer や solver 自体を完全には信用しない。

最終的な安全性判断に使う proof は、小さな proof kernel で再検証する。

```text
複雑な solver
    ↓
PIR certificate
    ↓
小さな verifier
    ↓
accept / reject
```

証明に失敗した場合は、

- Safe モードでは runtime check に戻る
- Verified モードではコンパイルエラー

とする。

optimizer の「たぶん正しい」を安全性根拠にしてはならない。

---

## 11. Web サーバーとの統合

### 11.1 外部入力は `Any + Untrusted`

```text
request.body
request.query
request.headers
request.cookies
```

は基本的に、

```text
Any
Untrusted(value)
```

から始まる。

JSON parse は構造を与えるが、信頼済みにはしない。

### 11.2 ルート宣言から validator を生成

```kond
route POST "/users" (
    body where
        self has {
            name: String where 1 <= len(self) <= 32,
            age: Int where 0 <= self < 150
        }
) {
    create_user(body)
}
```

コンパイラ/フレームワークは、

```text
JSON parse
object 判定
name 存在
String(name)
長さ範囲
age 存在
Int(age)
age 範囲
```

を自動生成できる。

成功後の handler では条件が証明済みになる。

### 11.3 SQL injection

危険な sink:

```kond
database.execute(query)
    requires SqlStatement(query)
```

普通の文字列は `SqlStatement` ではない。

```kond
database.execute(
    "SELECT ... '" + user_input + "'"
)
```

は証明できず拒否される。

構造化 SQL:

```kond
database.query(
    sql"SELECT * FROM users WHERE name = ${user_input}"
)
```

は補間値を parameterize し、

```text
SqlStatement(result)
```

を生成する。

### 11.4 XSS

HTML sink は、

```text
TrustedHtml(x)
HtmlText(x)
AttributeValue(x)
UrlAttribute(x)
```

など文脈ごとの条件を要求する。

```kond
html"<div>${user_input}</div>"
```

は HTML text context に応じた escaping を行い、適切な条件を生成する。

### 11.5 認証と認可

```text
Authenticated(user)
Owner(user, post)
HasRole(user, Admin)
CanDelete(user, post)
```

をすべて同じ Condition 系に置く。

```kond
fn delete_post(user, post)
    requires Authenticated(user)
    requires Owner(user, post) or HasRole(user, Admin)
```

認可チェック忘れは「必要条件を証明できない」というコンパイルエラーになる。

### 11.6 one-time security capability

```text
ValidResetToken(token)
OneTimeNonce(nonce)
```

を線形条件にすれば、一度使用した後は proof を消費できる。

### 11.7 標準 HTTP サーバーランタイム

Draft 0.2 の実行系は、`route` 宣言を標準 HTTP サーバーとして起動できる。

```sh
kond serve app.kd --bind 127.0.0.1 --port 8080
```

HTTP/JSON の外部ライブラリには依存しない。C++17 ランタイムとホスト OS のソケット API だけを使う。`--bind` の既定値は `127.0.0.1`、`--port` は `8080`、`--max-body` は `1048576` bytes である。`--once` を指定すると1接続だけ処理して終了する。HTTP/1.0 と HTTP/1.1 の origin-form を受け付け、1接続につき1応答を返して接続を閉じる。

`route METHOD "/path" (req) { ... }` はメソッドとパスの完全一致で登録される。パスが存在しなければ404、パスが一致してメソッドだけが違えば405と `Allow` ヘッダーを返す。`HEAD` ルートがない場合は対応する `GET` ハンドラを実行し、本文だけを抑制する。

ハンドラの `req` は `HttpRequest` であり、次のフィールド／メソッドを持つ。

```text
req.method       // String
req.target       // query を含むraw target
req.path         // query を除いたpath
req.query        // percent decode 済みの Object<String, String>
req.headers      // 小文字の名前を持つ Object<String, String>
req.cookies      // 小文字の名前を持つ Object<String, String>
req.body         // raw String
req.json()       // JSONをパースした値
req.header(name) // 1つのヘッダー、なければ Null
req.cookie(name) // 1つのcookie、なければ Null
```

リクエストの全フィールドと `req.json()` の全値には `Untrusted` が付く。JSON parse は構造の知識だけを追加し、endorse は行わない。不正JSON、失敗した `require`、ルート入力条件の失敗は HTTP 400 に変換される。既定のボディ上限はハンドラ実行前に検査され、超過時は413になる。

ルートの戻り値は次の規則でHTTP応答に変換される。

- `html"..."` は `text/html; charset=utf-8` とし、既存の文脈依存エスケープを使う。
- Object または List は JSON にシリアライズする。
- `json_response(value)` は `application/json` を返す。
- `http_response(status, headers, body)`（別名 `response`）はステータスとヘッダーを明示できる。
- その他の値は UTF-8 のテキストとして返し、`Null` は204になる。

Draft 0.2 のサーバーは逐次処理である。これはリファレンスランタイムおよびセキュリティ境界の実装であり、本番用の reverse proxy、TLS終端、接続プール、DBドライバではない。その用途では監査済みの前段を置き、標準ライブラリ契約と proof kernel の境界を維持する。

---

## 12. 謎数学的最適化

### 12.1 基本原則

optimizer は、

```text
現在の facts
+ 式
+ lemma database
```

から、証明可能な書き換えだけを行う。

### 12.2 例: 偶数

```text
Int(x)
x % 2 == 0
```

が証明済みなら、

```text
(x / 2) * 2
```

を、

```text
x
```

へ変換できる。

### 12.3 例: 範囲

```text
0 <= x < 256
```

なら、

```text
x & 255
```

を `x` にできる。

### 12.4 合同式

FIR に、

```text
x == 3 mod 8
```

を保持すれば、

```text
(x + 5) % 8
```

を `0` にできる。

alignment、loop induction、address 計算にも利用できる。

### 12.5 数学的性質

条件として、

```text
Prime(p)
Coprime(a, b)
Orthogonal(A)
Diagonal(A)
UnitVector(v)
Monotonic(f)
```

などを扱える。

例:

```text
Orthogonal(A)
=>
transpose(A) * A == Identity
```

### 12.6 e-graph

高度な optimizer は、

```text
式
↓
e-graph
↓
lemma による同値式追加
↓
cost model
↓
最安式を抽出
↓
PIR proof を生成
↓
proof kernel で検証
```

という構成を採用できる。

### 12.7 IEEE 浮動小数点

浮動小数点の変形には、

```text
Finite(x)
NotNaN(x)
NotNegativeZero(x)
```

など必要条件を明示する。

雑な global `fast-math` より、値ごとの証明済み性質から局所的に最適化許可を導出する。

---

## 13. 所有権による最適化

```text
UniqueBorrow(x)
NoAlias(x, y)
```

が証明できれば、

- redundant load elimination
- scalar replacement
- LICM
- vectorization
- store forwarding
- backend の noalias 相当

を安全に強化できる。

所有権は安全性の機能であると同時に optimizer への強力な事実供給源でもある。

---

## 14. エラーメッセージ仕様

Kond の診断は「型が違う」で終わってはならない。

必ず可能な限り次を示す。

```text
1. 何をしようとしたか
2. 何が必要だったか
3. 現在何が分かっているか
4. 何が足りないか
5. なぜ証明できなかったか
6. どう直せるか
```

### 14.1 必要条件不足

```text
error[E1204]: required condition could not be proven

   = operation requires:
       ValidUser(user)

   = currently known:
       Object(user)
       String(user.name)
       Int(user.age)

   = missing:
       0 <= user.age < 150

help: prove or check the missing condition
```

### 14.2 反例

```text
error[E1207]: condition is almost sufficient

   = required:
       x < 150

   = known:
       x <= 150

   = counterexample:
       x = 150
```

### 14.3 proof invalidation

```text
error[E1302]: previously proven condition was invalidated

 8 | check user.age >= 18
   |       -------------- proven for user0

10 | user.age = input()
   | ------------------ creates user1

12 | adult_only(user)
   | ^^^^^^^^^^^^^^^^ requires user1.age >= 18
```

### 14.4 ownership

```text
error[E2101]: value is no longer owned here

 5 | let y = move x
   |         ------ ownership transferred here

 7 | print(x)
   |       ^ requires Own(current_scope, x)
```

### 14.5 Unknown と Refuted を分ける

```text
error[E1501]: proof could not be completed

   = result:
       Unknown

   = reason:
       nonlinear arithmetic exceeded proof budget

note: Unknown does not mean the condition is false
```

---

## 15. 安全モード

### Safe

静的証明できないが runtime-decidable な条件について、自動 runtime check を許可する。

### Verified

必要条件が静的に証明されなければコンパイル失敗。

### Unsafe

明示的 `assume` と unsafe FFI を許可する。

unsafe は proof system を破壊するのではなく、「この証明の根拠は programmer trust である」と明示する境界とする。

---

## 16. 暫定構文

```kond
condition User(u) =
    u has { name, age } and
    u.name is String and
    u.age is Int

fn greet(raw)
    ensures result is String
{
    let user = json.parse(raw)

    require User(user)

    return "Hello, " + user.name
}
```

条件保存スロット:

```kond
let score where self is Int and self >= 0 = 0
```

所有権:

```kond
let y = move x
let shared = &y
let unique = &mut y
```

証明:

```kond
prove P
check P
require P

unsafe {
    assume P
}
```

---

## 17. 実装ロードマップ

### Phase 0

- parser
- Any runtime
- Condition expression
- if
- check
- 基本 runtime tag

### Phase 1

- SSA/value versioning
- FIR
- interval
- tag
- shape
- flow refinement

### Phase 2

- `let x where ...`
- invariant-preserving assignment
- transactional update
- Safe/Verified

### Phase 3

- ownership
- move
- shared/unique borrow
- linear fact environment

### Phase 4

- PIR
- trusted proof kernel
- proof certificate verification

### Phase 5

- Web-safe stdlib
- SQL/HTML/path/auth contracts
- security compile-fail tests

### Phase 6

- Cranelift/LLVM/WASM backend
- proof-driven specialization
- JIT guard elimination

### Phase 7

- congruence solver
- lemma database
- e-graph
- 謎数学最適化

### Phase 8

- optional SMT
- 外部 proof certificate
- theorem library

---

## 18. 仕様上の重要な非目標

Draft 0.2 では以下を確定しない。

- 完全な定理証明
- 任意数学式の自動証明
- module/package system
- macro system
- 完全な async/concurrency memory model
- FFI の詳細 ABI
- 標準ライブラリ全体
- surface syntax の最終形

まず「Any + Condition + Invariant + Linear Ownership + Proof IR」の意味論を固定する。

---

## 19. Kond の核

Kond のプログラムは、単なる値計算ではない。

```text
値を生成する
↓
値についての条件を証明する
↓
必要な線形権限を移動・消費する
↓
安全な操作を実行する
↓
新しい値と新しい条件を生成する
```

つまり、

> プログラムを実行するとは、値を計算しながら証明と所有権トークンを移動させることである。

このモデルによって、型安全・メモリ安全・入力安全・認可・リソース管理・最適化を、別々の仕組みではなく同じ「条件証明システム」の上に構築する。
---

## 20. 先行研究との位置づけ

Kond は個々の要素を新発明とは主張しない。Whiley の flow-sensitive/constrained types、Refinement/Liquid Types、Dafny の verification-aware contracts、Racket contracts、Rocq/Coq `sumbool`、Rust ownership、information-flow control/taint、PCC/FPCC、egg/equality saturation、Herbie の numerical rewriting と強く関係する。

狙う差分は、(1) `Any` を値世界の極端な基底にし `Int` 等まで知識側に寄せること、(2) refinement・invariant・ownership-derived capability・typestate・security・taint・optimization precondition を同じ Fact vocabulary に載せつつ判定器は専用化すること、(3) proof/certificate を verification の成果物だけでなく guard elimination・specialization・optimizer rewrite を支える compiler infrastructure にすること、である。

詳細は `docs/14-related-work.md` と `REFERENCES.md` を参照。

---

## 21. 標準ライブラリ

Draft 0.2 の参照インタプリタでは、import なしで `std.*` 名前空間を利用できる。
標準ライブラリは通常の可変Objectではなく、処理系が登録したAPIへの能力境界である。

```kd
let xs = std.core.range(0, 10)
let large = std.list.filter(xs, x => x >= 5)
let encoded = std.url.encode_component("a value")
```

提供する名前空間は次の通り。

- `std.core`: `clone`、`type_of`、`to_string`、`coalesce`、`range`、`repeat`
- `std.math`: `abs`、`sqrt`、`floor`、`ceil`、`round`、`pow`、`min`、`max`、`clamp`、`sign`、`gcd`、`lcm`
- `std.string`: 長さ、trim、大小文字変換、検索、split/join/replace、部分文字列、parse、repeat、reverse
- `std.list`: 長さ・アクセス、slice、append/prepend/concat、検索、filter/count/find、sum、sort、zip、`all`/`any`/`none`
- `std.map`: size、lookup、membership、keys/values/entries、永続的なput/remove、merge
- `std.json`: parse、stringify、pretty、is_valid
- `std.url`: component encode/decode、query parsing
- `std.html`: HTML text/attribute escape
- `std.security`: flow source、sanitizer、flow表示、unsafe境界操作
- `std.http`: response、json_response
- `std.io`: read_line、print、println
- `std.pred`: 型・数値・コレクション・等値・範囲・membership の再利用可能な条件
- `std.opt`: 読み込まれた最適化規則の診断情報

コレクションを返すAPIは引数を変更せず、新しい値を返す。`range` と slice は
`[start,end)`、文字列の `length`/index は Draft 0.2 ではUTF-8バイト単位である。
引数のflow labelは結果へjoinされ、標準APIが `Untrusted`/`Secret`/`Personal` を
勝手に除去することはない。整数overflow、JSON不正、index範囲外などは通常のKond
エラーになる。大量確保を防ぐため、参照実装の `range`/`repeat` には100万要素の上限がある。

`std.json.parse` は入力のflowを保持する。`std.html.escape` は `HtmlText`、
`std.url.encode_component` は `UrlComponent`、SQL parameterize API はSQL parameter
証拠を付与するが、これらは情報flowのdeclassifyではない。

`std.list.filter`/`count_if`/`find` は `x => condition` ラムダを受け取り、
`std.list.all`/`any`/`none` と `std.pred.*` は `if`、`check`、`require` で条件として使える。
値式として使ったnamespace predicateはruntime `Bool`になる。
これは `true`/`false` のソースリテラルや静的なprimitive bool型を追加する意味ではなく、
JSONなどと同じruntime data boundaryでの表現である。条件位置では引き続き命題付きの
`Condition<P>` として扱う。

詳細なAPI一覧、エラー、flow/safety、互換性方針は `docs/15-standard-library.md` に定める。

## 22. ユーザー定義最適化ライブラリ

最適化ライブラリは `condition` と `rewrite` だけを含む `.kd` ファイルである。
`--opt-lib FILE` で明示的にロードする。

```kd
rewrite exact divisible_roundtrip(x, d) when d > 0 and x % d == 0 {
    from (x / d) * d
    to x
}
```

`from` の式を現在の式木へmatchし、宣言済みparameterへ束縛する。束縛を代入した
`when` 条件が現在のfact環境で `Proven` のときだけ、`to` を評価して置換する。
参照実装では `exact` (`ExactEq`) のみ実行し、効果を持つ式はmatch対象から除外する。
純粋な標準ライブラリ呼び出しには閉じたeffect summaryがあり、I/O、mutation、ユーザー関数、
move、borrowは最適化対象にしない。
候補の評価に失敗した場合は元の通常実行へ戻るため、未証明の規則が意味論を変えない。

`real`、`approx`、`heuristic` も構文上登録できるが、`ExactEq`へ暗黙昇格せず、
Draft 0.2 では診断用の非実行候補である。`--explain-optimizations` は適用規則と
proof classを表示し、`std.opt.rule_count()`/`std.opt.rule_names()` はレジストリを検査する。
リポジトリの `std/optimization.kd` は30個の整数ExactEq規則を提供する。コアoptimizerも
checked constant folding、算術・bitwiseのzero/one/self identity、純粋な標準API呼び出しの
constant folding、range/repeat/concat/sort/sliceなどの中間Listを作らないlength計算を行う。
