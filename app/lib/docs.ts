export type DocSection = {
  heading: string;
  paragraphs: string[];
  code?: string;
  codeLang?: "kond" | "console";
  codeTitle?: string;
  note?: string;
  bullets?: string[];
};

export type Doc = {
  slug: string;
  number: string;
  category: string;
  title: string;
  shortTitle: string;
  description: string;
  readTime: string;
  color: "lime" | "cyan" | "amber";
  sections: DocSection[];
};

export const docs: Doc[] = [
  {
    slug: "core-model",
    number: "01",
    category: "FOUNDATIONS",
    title: "Core model",
    shortTitle: "Core model",
    description: "Kond の世界は、値と、その値についての知識からできています。",
    readTime: "8 min read",
    color: "lime",
    sections: [
      {
        heading: "Any is the starting point",
        paragraphs: [
          "Kond では、すべての実行時値が universal value domain `Any` に属します。`Int` や `String` は固定された静的型ではなく、値についての predicate です。",
          "入力を受け取った直後、プログラムが知っているのは `Value(x)` と、外部入力なら通常 `Untrusted(x)` だけ。必要な知識を、必要な場所で積み上げていきます。",
        ],
        code: "let x = input()\ncheck x is Int\nrequire x > 0",
        note: "型を先に決めるのではなく、値が満たす条件を証明する。これが Kond の出発点です。",
      },
      {
        heading: "Conditions are values",
        paragraphs: [
          "論理式は組み込みの Boolean ではありません。評価結果は `Condition<P>` という、命題 `P` に対する証拠を運ぶ値です。",
          "条件は値のバージョンに束縛されます。値が更新されたあとに、以前の証明を新しい値へ流用することはできません。",
        ],
        code: "let price = 10\nlet affordable = price < 100\nprice = 1000\n// affordable proves nothing about price₁",
      },
      {
        heading: "Three states, two runtime outcomes",
        paragraphs: [
          "コンパイル時の proof state は `Proven`、`Refuted`、`Unknown` の三つです。ただし実行時の条件評価は二つの結果だけを返します。",
        ],
        bullets: [
          "`holds(P)` — `P` の証拠を現在の分岐へ追加する",
          "`fails(P)` — `not P` の証拠を現在の分岐へ追加する",
          "`Unknown(P)` — 安全モードでは runtime check、verified モードではエラー",
        ],
      },
    ],
  },
  {
    slug: "conditions",
    number: "02",
    category: "FOUNDATIONS",
    title: "The condition system",
    shortTitle: "Conditions",
    description: "条件を組み合わせ、検査し、証明として消費するためのルール。",
    readTime: "10 min read",
    color: "lime",
    sections: [
      {
        heading: "A common vocabulary for knowledge",
        paragraphs: [
          "`is`、比較、`and` / `or` / `not`、shape schema、名前付き condition が同じ condition algebra に入ります。ひとつの万能 solver ではなく、領域ごとに予測可能な decision procedure を選びます。",
        ],
        code: "condition NonEmpty(xs) = xs is List and length(xs) > 0\n\ncheck NonEmpty(items)\nrequire items[0] is Object",
      },
      {
        heading: "Proofs have an owner",
        paragraphs: [
          "証明は対象の value version を参照します。`x₀` に対する `x > 0` の証拠は、代入後の `x₁` について何も保証しません。",
          "この version binding が、条件付き mutable slot と transactional update の安全性を支えています。",
        ],
        note: "条件は「真らしいメモ」ではなく、どの値についての知識かが追跡できる first-class value です。",
      },
    ],
  },
  {
    slug: "control-flow",
    number: "03",
    category: "LANGUAGE",
    title: "Control flow",
    shortTitle: "Control flow",
    description: "分岐とループの中で、証明がどのように流れ、合流するか。",
    readTime: "9 min read",
    color: "cyan",
    sections: [
      {
        heading: "Branches refine the current world",
        paragraphs: [
          "`if` の `holds(P)` 側では `P`、`fails(P)` 側では `not P` が fact environment に入ります。分岐を抜けると、両方の経路で成立する知識だけが残ります。",
        ],
        code: "if score >= 80 {\n    award(\"gold\") // score >= 80\n} else {\n    retry()       // not(score >= 80)\n}",
      },
      {
        heading: "Loops need a stable story",
        paragraphs: [
          "ループの不変条件は、各反復の入口と出口で維持されなければなりません。Kond は、条件を消費してしまう処理と、次の反復へ持ち越せる処理を分けて考えます。",
        ],
        code: "let i where self >= 0 = 0\nwhile i < items.length invariant i >= 0 {\n    visit(items[i])\n    i += 1\n}",
      },
    ],
  },
  {
    slug: "ownership",
    number: "05",
    category: "RESOURCES",
    title: "Ownership & borrow",
    shortTitle: "Ownership",
    description: "リソースを誰が持ち、いつ借り、どこで返すのかを決定的に追跡します。",
    readTime: "12 min read",
    color: "cyan",
    sections: [
      {
        heading: "Resources are linear conditions",
        paragraphs: [
          "所有権は通常の predicate ではなく、消費される `LinearCondition` として扱われます。値を `move` したあと、元の binding は同じリソースを使えません。",
          "これは GC の有無を規定する機能ではありません。実行前に、リソースの状態遷移が一意に読めることを保証するためのモデルです。",
        ],
        code: "let file = open(\"notes.txt\")\nlet contents = read(move file)\nclose(file) // E2101: file was moved",
        note: "所有権の履歴は `Own -> Moved`、`Own -> SharedBorrow`、`BorrowEnd -> Own(vN)` のような証明トレースになります。",
      },
      {
        heading: "Borrowing has a lexical end",
        paragraphs: [
          "共有借用と可変借用はスコープに結びつきます。借用が終わると、所有権は決定的に復元されます。曖昧な runtime lifetime に頼らないため、診断の再現性も保たれます。",
        ],
        code: "let account = load_account()\n{\n    let view = borrow account\n    print(view.name)\n}\nupdate(move account)",
      },
    ],
  },
  {
    slug: "security",
    number: "08",
    category: "SECURITY",
    title: "Web security",
    shortTitle: "Web security",
    description: "入力境界・taint・HTML・SQL を、同じ証明の流れで扱う。",
    readTime: "11 min read",
    color: "amber",
    sections: [
      {
        heading: "Taint is knowledge too",
        paragraphs: [
          "外部から来た値には `Untrusted` や flow label が付きます。値の形を検査しても、その出所の情報は自動では消えません。安全な sink へ渡すには、文脈に応じた sanitizer が必要です。",
        ],
        code: "let name = untrusted(input())\nlet page = html\"<h1>${name}</h1>\"\n// context escape preserves the security contract",
      },
      {
        heading: "Explicit boundaries",
        paragraphs: [
          "`html` template は HTML 文脈でエスケープし、`sql` template は補間値を parameterize します。taint を消す `declassify` や `endorse` は unsafe boundary の中でのみ明示できます。",
        ],
        note: "安全な既定値と、レビュー可能な信頼境界。セキュリティ機能も Kond の condition vocabulary の一部です。",
      },
    ],
  },
  {
    slug: "optimization",
    number: "09",
    category: "PERFORMANCE",
    title: "Proof-backed optimization",
    shortTitle: "Optimization",
    description: "意味論を変えないと証明できた書き換えだけを、速いコードにする。",
    readTime: "8 min read",
    color: "amber",
    sections: [
      {
        heading: "The proof comes first",
        paragraphs: [
          "Kond の optimizer は、速そうだから書き換えるのではありません。PIR の proof class と証明を検証し、意味論が一致する exact rewrite だけを適用します。",
        ],
        code: "// given: d > 0 and x % d == 0\n(x / d) * d  =>  x       // ExactEq\n\n// given: x >= 0\nabs(x)       =>  x       // ExactEq",
      },
      {
        heading: "Exact, real, approximate",
        paragraphs: [
          "rewrite には `exact`、`real`、`approx`、`heuristic` のクラスがあります。現在の Draft 0.2 では、意味論の置換として扱えるのは証明を持つ exact rewrite が中心です。",
        ],
        bullets: [
          "ExactEq — すべての許される状態で同じ結果",
          "RealEq — 実数モデル上で一致する書き換え",
          "Approx — 誤差境界を明示した近似",
          "Heuristic — 性能の期待であり、意味論の証明ではない",
        ],
      },
    ],
  },
  {
    slug: "diagnostics",
    number: "10",
    category: "REFERENCE",
    title: "Diagnostics",
    shortTitle: "Diagnostics",
    description: "証明できなかった理由を、次に進めるための情報として返します。",
    readTime: "6 min read",
    color: "lime",
    sections: [
      {
        heading: "Unknown is actionable",
        paragraphs: [
          "verified mode で `Unknown` に出会ったとき、Kond は単に「型エラー」とは言いません。必要だった predicate、現在知られている fact、証明が途切れた value version を診断へ含めます。",
        ],
        code: "error[E2204]: condition is unknown\n  required:  items.length > 0\n  known:     items is List\n  hint:      check NonEmpty(items) before indexing",
        codeLang: "console",
        codeTitle: "kond check",
      },
      {
        heading: "A small trusted kernel",
        paragraphs: [
          "コンパイラが生成する証明は、最後に小さな proof kernel が検証します。最適化や解析が増えても、信頼する検証面を小さく保つことが目標です。",
        ],
      },
    ],
  },
];

export const featuredDocs = docs.filter((doc) => ["core-model", "ownership", "optimization"].includes(doc.slug));

export const docGroups = [
  { label: "Foundations", slugs: ["core-model", "conditions"] },
  { label: "Language", slugs: ["control-flow"] },
  { label: "Resources", slugs: ["ownership"] },
  { label: "Security", slugs: ["security"] },
  { label: "Performance", slugs: ["optimization"] },
  { label: "Reference", slugs: ["diagnostics"] },
];

export function sectionId(heading: string) {
  return heading
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "");
}

export function getDoc(slug: string) {
  return docs.find((doc) => doc.slug === slug);
}
