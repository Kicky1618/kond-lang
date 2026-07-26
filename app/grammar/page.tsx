import type { Metadata } from "next";
import { CodeBlock } from "@/app/components/code-block";
import { getGrammar } from "@/app/lib/spec";

export const metadata: Metadata = { title: "文法リファレンス", description: "Kond Draft 0.2 の provisional EBNF と主要構文へのアンカー。", alternates: { canonical: "https://kicky1618.github.io/kond-lang/grammar/" } };

const landmarks = [
  ["condition declaration", "condition_decl", 6], ["invariant-bearing let", "let_stmt", 33],
  ["function contract", "contract_clause", 22], ["route declaration", "route_decl", 16],
  ["tagged string", "tagged_string", 84], ["lambda", "lambda_expr", 65],
  ["chained comparison", "chained_comparison", 61], ["ownership syntax", "unary_expr", 70],
  ["proof statements", "proof_stmt", 35]
] as const;

export default function GrammarPage() {
  const grammar = getGrammar();
  return <main className="standalone wide" id="main">
    <header className="page-hero compact"><span>SYNTAX.EBNF · PROVISIONAL</span><h1>Draft surface<br /><em>grammar.</em></h1><p>この文法は <code>spec/syntax.ebnf</code> から直接表示しています。仕様自身が「provisional syntactic coverage grammar」としており、意味論の完全な代替ではありません。</p></header>
    <nav aria-label="主要構文" className="grammar-landmarks">{landmarks.map(([label, rule]) => <a href={`#${rule.replaceAll("_", "-")}`} key={rule}><span>{label}</span><code>{rule}</code></a>)}</nav>
    <section className="grammar-source"><CodeBlock code={grammar} filename="spec/syntax.ebnf" language="ebnf" lineIds={Object.fromEntries(landmarks.map(([, rule, line]) => [line, rule.replaceAll("_", "-")]))} /></section>
  </main>;
}
