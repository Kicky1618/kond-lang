import type { Metadata } from "next";
import Link from "next/link";
import { CodeBlock } from "@/app/components/code-block";
import { ConditionDiagram, OwnershipDiagram, ProofPipeline } from "@/app/components/diagrams";
import { ArrowIcon } from "@/app/components/icons";

export const metadata: Metadata = { title: "言語設計の概要", description: "Kond の Any、Condition、ownership、Proof IR を仕様に沿って俯瞰する非規範的な入門。", alternates: { canonical: "https://kicky1618.github.io/kond-lang/overview/" } };

const topics = [
  ["なぜすべてが Any なのか", "Kond の実行時値は universal value domain Any に属します。Int や String は排他的な静的型ではなく、値について成立し得る predicate です。これにより外部入力、部分的な知識、段階的な検証を同じ値モデルで扱います。", "/docs/core-model/"],
  ["Condition<P> とは何か", "命題 P の成立・不成立について evidence を運ぶ first-class value です。条件値は生成時の value version に束縛され、値が変わった後の版へ古い証拠を流用できません。", "/docs/runtime-conditions/"],
  ["bool をどう置き換えるのか", "比較や論理式の実行時結果は Holds(Proof<P>) または Fails(Proof<not P>) です。コンパイル時の Unknown は第三の実行時 truth value ではなく、safe mode では check、verified mode では診断になります。", "/docs/condition-system/"],
  ["条件付き変数はなぜ mutable なのか", "Kond では mut という印ではなく invariant-bearing slot が mutation を導入します。代入候補を先に検証し、条件を満たす場合だけ commit するため、slot は更新後も宣言した条件を保ちます。", "/docs/mutation-invariants/"],
  ["ownership を linear condition として扱う理由", "Own、borrow、one-time capability は複製できない LinearCondition です。ただし Draft 0.2 は万能 solver ではなく、専用の決定的 borrow checker を必須とします。", "/docs/deterministic-ownership/"],
  ["Proof IR が保証すること", "CIR は制御フロー、FIR は fact、PIR は検査可能な proof object を表し、最終的に小さな kernel が証明を検証します。外部 solver を無条件に信頼する設計ではありません。", "/docs/proof-ir/"],
  ["Web サーバーとの相性", "ルート境界の入力を Any + Untrusted として開始し、validator、flow label、context-specific template、sink contract へつなげます。標準 HTTP runtime は参照実装であり、本番用 reverse proxy 等を主張しません。", "/docs/web-security/"],
  ["数学的最適化との関係", "候補生成と semantic justification を分離し、ExactEq、RealEq、ApproxEq、HeuristicImprovement を混同しません。意味論的な置換として適用するには適切な proof class が必要です。", "/docs/exact-approx-proofs/"]
];

export default function OverviewPage() {
  return <main className="standalone" id="main">
    <header className="page-hero"><span>LANGUAGE OVERVIEW · 非規範的説明</span><h1>Any から Proof へ。<br /><em>Kond の設計を一望する。</em></h1><p>このページは仕様書を読み始めるための解説です。規範的な定義は、リンク先のリポジトリ由来仕様本文を参照してください。</p></header>
    <section className="overview-lead"><ConditionDiagram /><CodeBlock code={`let value = input()\ncheck value is Int\nrequire 0 <= value < 150`} filename="knowledge.kd" language="kond" /></section>
    <section className="topic-list">{topics.map(([title, body, href], index) => <article key={title}><span>{String(index + 1).padStart(2, "0")}</span><div><h2>{title}</h2><p>{body}</p><Link href={href}>仕様の該当章 <ArrowIcon /></Link></div></article>)}</section>
    <section className="overview-diagrams"><div><h2>Resource world</h2><OwnershipDiagram /></div><div><h2>Trusted path</h2><ProofPipeline /></div></section>
  </main>;
}
