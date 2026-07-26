import type { Metadata } from "next";
import { Markdown } from "@/app/components/markdown";
import { getDoc } from "@/app/lib/spec";

export const metadata: Metadata = { title: "Related Work", description: "Kond の設計上の位置づけと、仕様書に列挙された既存研究への参照。", alternates: { canonical: "https://kicky1618.github.io/kond-lang/related-work/" } };

export default function RelatedWorkPage() {
  const related = getDoc("related-work");
  const references = getDoc("references");
  if (!related || !references) return null;
  return <main className="standalone" id="main"><header className="page-hero compact"><span>POSITIONING · SOURCE-BOUND</span><h1>Related work,<br /><em>without overclaiming.</em></h1><p>以下は <code>docs/14-related-work.md</code> と <code>REFERENCES.md</code> をそのまま情報源として表示します。外部リンクも REFERENCES に含まれるものだけです。</p></header><section className="source-panel"><Markdown source={related.body} /></section><section className="source-panel references-panel"><Markdown source={references.body} /></section></main>;
}
