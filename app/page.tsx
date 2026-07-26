import Link from "next/link";
import { CodeBlock } from "./components/code-block";
import { ConditionDiagram, OwnershipDiagram, ProofPipeline } from "./components/diagrams";
import { ArrowIcon } from "./components/icons";
import { DraftBadge } from "./components/navigation";
import { getDraft } from "./lib/spec";

const concepts = [
  ["Value world", "Any", "すべての実行時値が属する普遍的な値領域"],
  ["Knowledge world", "Condition<P>", "値について証明された知識を運ぶ条件"],
  ["Runtime decision", "Holds(Proof<P>) | Fails(Proof<not P>)", "実行時には証拠を持つ二つの結果"],
  ["Resource world", "LinearCondition<L>", "消費・移動される所有権と能力"],
  ["Mutable slot", "Invariant<P>", "更新の前後で維持される条件"]
];

const features = [
  ["01", "Any-first value model", "値を先に分類せず、必要になった知識を predicate と証拠として追加します。", "/docs/core-model/"],
  ["02", "Evidence-carrying conditions", "条件評価は bool ではなく、成立または不成立の証拠を運びます。", "/docs/runtime-conditions/"],
  ["03", "Condition-preserving mutation", "mutable slot は不変条件を持ち、代入ごとにその条件を再証明します。", "/docs/mutation-invariants/"],
  ["04", "Ownership as linear conditions", "move と borrow を消費可能な線形 fact として統一的に表現します。", "/docs/ownership-linear/"],
  ["05", "Web input safety", "入力境界、taint、HTML、SQL sink を明示的な条件として追跡します。", "/docs/web-security/"],
  ["06", "Proof-backed optimization", "意味論を変えないことを証明できた書き換えだけを適用します。", "/docs/optimization/"]
];

const heroCode = `let request = input()
check request is Object
check request has "age"

if request.age is Int and 0 <= request.age < 150 {
    register(request.age)
} else {
    reject("invalid age")
}`;

export default function HomePage() {
  const draft = getDraft();
  return <main id="main">
    <section className="hero">
      <div className="hero-grid" />
      <div className="hero-copy">
        <div className="eyebrow"><span /> EXPERIMENTAL LANGUAGE SPECIFICATION</div>
        <h1>Values are open.<br /><em>Knowledge is proven.</em></h1>
        <p>Kond は、値を <code>Any</code> として扱い、値についての知識、所有権、安全性、最適化を「証拠」の流れとして記述する実験的プログラミング言語です。</p>
        <div className="hero-actions"><Link className="button primary" href="/docs/specification-ja/">仕様を読む <ArrowIcon /></Link><Link className="button" href="/overview/">言語設計の概要</Link></div>
        <div className="hero-meta"><DraftBadge draft={draft} /><span>日本語 / English</span><span>SPECIFICATION AS SOURCE</span></div>
      </div>
      <div className="hero-code"><CodeBlock code={heroCode} filename="boundary.kd" language="kond" highlight={[2, 3, 5]} /><div className="verified-chip"><span>✓</span><div><small>PROOF STATE</small><strong>all required conditions available</strong></div></div></div>
    </section>

    <section className="concept-strip">
      <div className="section-heading"><span>00 / CORE MODEL</span><h2>ひとつのプログラム、<br />ふたつの世界。</h2><p>値そのものと、値について現在わかっていることを分離する。それが Kond の出発点です。</p></div>
      <div className="concept-list">{concepts.map(([label, value, description], index) => <article key={label}><span>{String(index + 1).padStart(2, "0")}</span><div><small>{label}</small><code>{value}</code><p>{description}</p></div></article>)}</div>
    </section>

    <section className="diagram-section">
      <div className="section-intro"><span>01 / EVIDENCE FLOW</span><h2>検査は、知識を<br />次の操作へ渡す。</h2><p>非規範的な図解。正確な意味論は各仕様章を参照してください。</p><Link href="/docs/condition-system/">Condition system を読む <ArrowIcon /></Link></div>
      <ConditionDiagram />
    </section>

    <section className="features-section">
      <div className="section-title-row"><div><span>02 / LANGUAGE SURFACE</span><h2>安全性を、同じ語彙で。</h2></div><p>型検査、リソース管理、セキュリティ、最適化を別々の注釈系にせず、Condition と Proof の共通基盤へ接続します。</p></div>
      <div className="feature-grid">{features.map(([number, title, description, href]) => <Link href={href} key={number}><span>{number}</span><h3>{title}</h3><p>{description}</p><b>EXPLORE <ArrowIcon /></b></Link>)}</div>
    </section>

    <section className="systems-section">
      <article><div><span>03 / RESOURCE WORLD</span><h2>Ownership,<br />made explicit.</h2><p>所有権 checker は決定的な decision procedure を使い、その結果を共通 fact system に接続します。</p><Link href="/docs/ownership-linear/">所有権仕様へ <ArrowIcon /></Link></div><OwnershipDiagram /></article>
      <article><div><span>04 / TRUSTED PATH</span><h2>A small kernel,<br />a visible chain.</h2><p>Source から生成された証明は PIR を経由し、小さな trusted kernel で検査されます。</p><Link href="/docs/proof-ir/">Proof IR を読む <ArrowIcon /></Link></div><ProofPipeline /></article>
    </section>

    <section className="start-section">
      <span>START READING</span><h2>仕様を、一本道ではなく<br />理解の地図として。</h2><p>まず日本語の統合仕様を読むか、関心のある章から始めてください。サイトの本文はリポジトリ内 Markdown から直接生成されています。</p>
      <div><Link className="button primary" href="/docs/specification-ja/">日本語仕様書 <ArrowIcon /></Link><Link className="button" href="/playground/">サンプルを見る</Link><a className="button" href="https://github.com/Kicky1618/kond-lang" rel="noreferrer noopener" target="_blank">GitHub ↗</a></div>
    </section>
  </main>;
}
