import type { Metadata } from "next";
import { DiagnosticBlock, type Diagnostic } from "@/app/components/diagnostic-block";

export const metadata: Metadata = { title: "診断カタログ", description: "Kond Draft 0.2 の条件、所有権、セキュリティ、最適化診断。", alternates: { canonical: "https://kicky1618.github.io/kond-lang/diagnostics/" } };

const categories = [
  ["Conditions / Proofs", "E1101 · E1104 · E1204 · E1207 · E1302 · E1401 · E1501"],
  ["Ownership / Borrowing", "E2101 · E2203 · E2205 · E2301"],
  ["Security / Trust", "E3102 · E3201 · E3204"],
  ["Optimization", "E4101 · E4102 · E0113–E0119"],
  ["Effects / Resource State", "E5xxx (reserved category)"]
];

const diagnostics: Diagnostic[] = [
  {
    code: "E1101", title: "initial value violates variable invariant", category: "Conditions / Proofs",
    source: "let age where self is Int and 0 <= self < 150 = 200",
    output: `error[E1101]: initial value violates variable invariant
  --> src/main.kd:3:45
   |
 3 | let age where self is Int and 0 <= self < 150 = 200
   |                                                  ^^^
   |
   = required:
       Int(200)
       0 <= 200
       200 < 150

   = disproven:
       200 < 150

   = invariant accepts:
       Int intersect [0, 150)

   = provided:
       200`,
    cause: "初期値 200 が slot の invariant である半開区間 [0, 150) を満たしません。",
    fix: "invariant を満たす初期値を渡すか、宣言した範囲が意図どおりかを確認します。"
  },
  {
    code: "E1204", title: "required condition could not be proven", category: "Conditions / Proofs",
    output: `error[E1204]: required condition could not be proven
  --> src/user.kd:18:5

   = operation requires:
       ValidUser(user)

   = currently known:
       Object(user)
       HasField(user, "name")
       String(user.name)
       HasField(user, "age")
       Int(user.age)

   = missing:
       0 <= user.age < 150

help: prove or check the missing condition before this operation`,
    cause: "操作に必要な ValidUser(user) のうち、age の範囲条件が fact environment にありません。",
    fix: "操作の前で不足条件を prove するか、実行時 check / require で検証します。"
  },
  {
    code: "E1207", title: "condition is almost sufficient", category: "Conditions / Proofs",
    output: `error[E1207]: condition is almost sufficient

   = required:
       x < 150

   = known:
       x <= 150

   = counterexample:
       x = 150`,
    cause: "既知条件は上限を含みますが、必要条件は上限を含みません。x = 150 が反例です。",
    fix: "境界を x < 150 に狭めるか、操作側の契約が x <= 150 でよいかを見直します。"
  },
  {
    code: "E1302", title: "previously proven condition was invalidated", category: "Conditions / Proofs",
    output: `error[E1302]: previously proven condition was invalidated

 8 | check user.age >= 18
   |       -------------- proven for user0 here

10 | user.age = input()
   | ------------------ creates user1 and invalidates field-dependent proof

12 | adult_only(user)
   | ^^^^^^^^^^^^^^^^ requires user1.age >= 18

   = old proof:
       user0.age >= 18

   = current value:
       user1`,
    cause: "証明は user₀ の値バージョンに束縛されています。代入で user₁ になり、古い証明は失効しました。",
    fix: "更新後の user₁.age を再度 check / prove してから adult_only を呼びます。"
  },
  {
    code: "E2101", title: "value is no longer owned here", category: "Ownership / Borrowing",
    output: `error[E2101]: value is no longer owned here

 5 | let y = move x
   |         ------ ownership transferred here

 7 | print(x)
   |       ^ requires Own(current_scope, x)

   = current ownership:
       Own(current_scope, y)

help: use \`y\` or restructure the move`,
    cause: "move x により linear fact Own(x) は y へ移り、元の binding では利用できません。",
    fix: "移動先 y を使うか、move の位置とデータフローを組み替えます。"
  },
  {
    code: "E2301", title: "linear condition was already consumed", category: "Ownership / Borrowing",
    output: `error[E2301]: linear condition was already consumed

30 | reset_password(token)
   |                ----- ValidResetToken(token) consumed here

32 | reset_password(token)
   |                ^^^^^ required again here

   = required:
       Own(current_scope, token)
       ValidResetToken(token)

   = available linear facts:
       none`,
    cause: "一回限りの ValidResetToken(token) は最初の呼び出しですでに消費されています。",
    fix: "同じ token の二重使用をなくし、処理が一度だけ実行される制御フローにします。"
  },
  {
    code: "E3201", title: "untrusted data cannot be used as trusted HTML", category: "Security / Trust",
    output: `error[E3201]: untrusted data cannot be used as trusted HTML

   = required:
       TrustedHtml(value)

   = known:
       String(value)
       Untrusted(value)

   = no verified transformation:
       Untrusted -> TrustedHtml

help: use a context-aware HTML template or escaping function`,
    cause: "Untrusted な文字列から TrustedHtml への検証済み変換がないまま HTML sink へ渡しています。",
    fix: "context-aware な html tagged template または仕様で認められた escaping function を使います。"
  },
  {
    code: "E1501", title: "proof could not be completed", category: "Conditions / Proofs",
    output: `error[E1501]: proof could not be completed

   = goal:
       x*x + y*y >= 2*x*y

   = assumptions:
       Real(x)
       Real(y)

   = solver:
       nonlinear arithmetic

   = result:
       Unknown

   = reason:
       proof budget exhausted

note: \`Unknown\` does not mean the condition is false`,
    cause: "非線形算術 solver が proof budget 内に結論を得られませんでした。Unknown は反証ではありません。",
    fix: "補題や追加条件で goal を単純化するか、許される範囲で proof budget / solver 構成を見直します。"
  }
];

export default function DiagnosticsPage() {
  return <main className="standalone" id="main">
    <header className="page-hero compact"><span>DIAGNOSTIC SPECIFICATION · NORMATIVE EXAMPLES</span><h1>Errors that explain<br /><em>the missing proof.</em></h1><p>コード、タイトル、完全な出力は <code>spec/docs/10-diagnostics.md</code> に記載された例です。「原因」と「修正方針」はそれを補う非規範的説明です。</p></header>
    <section className="category-grid">{categories.map(([name, codes]) => <article key={name}><h2>{name}</h2><code>{codes}</code></article>)}</section>
    <section className="diagnostic-list">{diagnostics.map((diagnostic) => <DiagnosticBlock diagnostic={diagnostic} key={diagnostic.code} />)}</section>
  </main>;
}
