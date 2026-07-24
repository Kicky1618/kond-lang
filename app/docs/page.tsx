import Link from "next/link";
import DocsFrame from "../components/docs-frame";
import { ArrowUpRight } from "../components/icons";
import { Breadcrumbs, Eyebrow, Pill } from "../components/ui";
import { docs, featuredDocs } from "../lib/docs";

export default function DocsOverviewPage() {
  return (
    <DocsFrame>
      <main className="min-w-0">
        <Breadcrumbs items={[{ label: "Docs" }, { active: true, label: "Overview" }]} />

        <section className="mt-7 max-w-[760px]">
          <Eyebrow>Start here</Eyebrow>
          <h2 className="mt-4 text-[clamp(2.1rem,4vw,3.6rem)] font-650 leading-[1] tracking-[-0.075em] text-white">Kond を理解するための、<span className="text-lime">3 つの入口。</span></h2>
          <p className="mt-6 max-w-[640px] text-[15px] leading-[1.9] text-[#9ba7a2]">Kond の中心にあるのは、値を変えるたびに「その値について何が証明されているか」を更新する考え方です。下の三つから読み始めると、仕様全体の輪郭がつかめます。</p>
        </section>

        <section className="mt-12 grid gap-4 md:grid-cols-3">
          {featuredDocs.map((doc, index) => (
            <Link className="group relative overflow-hidden rounded-[16px] border border-white/10 bg-[#11171c] p-5 transition-all duration-300 hover:-translate-y-1 hover:border-white/25 sm:p-6" href={`/docs/${doc.slug}`} key={doc.slug}>
              <div className="flex items-start justify-between">
                <Pill tone={doc.color}>{doc.number} / {doc.category}</Pill>
                <ArrowUpRight className="h-4 w-4 text-[#61706a] transition-colors group-hover:text-lime" />
              </div>
              <div className="mt-15">
                <p className="font-mono text-[10px] text-[#697670]">0{index + 1} / RECOMMENDED</p>
                <h3 className="mt-3 text-[21px] font-650 tracking-[-0.05em] text-white">{doc.title}</h3>
                <p className="mt-3 min-h-[3.4rem] text-[12px] leading-[1.75] text-[#87938f]">{doc.description}</p>
              </div>
              <div className="mt-6 flex items-center justify-between border-t border-white/10 pt-4 font-mono text-[9px] uppercase tracking-[0.12em] text-[#697670]">
                <span>{doc.readTime}</span>
                <span className="text-[#a3afa9] transition-colors group-hover:text-lime">Read chapter →</span>
              </div>
            </Link>
          ))}
        </section>

        <section className="mt-14 overflow-hidden rounded-[16px] border border-lime/18 bg-[#111a17]">
          <div className="grid gap-8 p-5 sm:p-7 lg:grid-cols-[0.8fr_1.2fr] lg:items-center lg:p-9">
            <div>
              <p className="font-mono text-[10px] uppercase tracking-[0.16em] text-lime">The core thesis</p>
              <h2 className="mt-4 text-[clamp(1.7rem,3vw,2.7rem)] font-650 leading-[1.05] tracking-[-0.065em] text-white">値の世界と、<br />知識の世界。</h2>
              <p className="mt-5 max-w-[360px] text-[13px] leading-[1.8] text-[#8f9c96]">Kond はこの二つを混ぜずに、証明でつなぎます。</p>
            </div>
            <div className="relative overflow-hidden rounded-[12px] border border-white/10 bg-ink p-5 font-mono text-[11px] leading-[2] sm:p-6 sm:text-[12px]">
              <span className="absolute right-0 top-0 h-full w-1/2 bg-gradient-to-l from-lime/7 to-transparent" />
              <div className="relative grid gap-4 sm:grid-cols-[1fr_auto_1fr] sm:items-center sm:gap-5">
                <div>
                  <p className="mb-2 text-[9px] uppercase tracking-[0.16em] text-[#64716c]">Value world</p>
                  <p className="text-white">Any</p>
                  <p className="text-[#8c9993]">x, order, request</p>
                </div>
                <div className="flex items-center gap-2 text-lime"><span className="h-px w-7 bg-lime/50" /><span>proof</span><span className="h-px w-7 bg-lime/50" /></div>
                <div>
                  <p className="mb-2 text-[9px] uppercase tracking-[0.16em] text-[#64716c]">Knowledge world</p>
                  <p className="text-lime">Condition&lt;P&gt;</p>
                  <p className="text-[#8c9993]">Holds / Fails / Unknown</p>
                </div>
              </div>
              <div className="relative mt-6 border-t border-white/10 pt-4 text-[#77847d]">Mutation = preserve invariant · Execution = prove requirements · Optimization = rewrite with proof</div>
            </div>
          </div>
        </section>

        <section className="mt-18" id="all-docs">
          <div className="flex items-end justify-between gap-4 border-b border-white/10 pb-4">
            <div>
              <Eyebrow>Full index</Eyebrow>
              <h2 className="mt-3 text-2xl font-650 tracking-[-0.05em] text-white">All chapters</h2>
            </div>
            <span className="font-mono text-[10px] text-[#68756f]">{docs.length} chapters</span>
          </div>
          <div className="divide-y divide-white/8">
            {docs.map((doc) => (
              <Link className="group grid gap-3 py-5 transition-colors hover:bg-white/[0.02] sm:grid-cols-[50px_150px_minmax(0,1fr)_auto] sm:items-center sm:gap-5" href={`/docs/${doc.slug}`} key={doc.slug}>
                <span className="font-mono text-[11px] text-[#68756f]">{doc.number}</span>
                <Pill className="w-fit tracking-[0.11em]" tone={doc.color}>{doc.category}</Pill>
                <span><span className="block text-[15px] font-600 tracking-[-0.02em] text-white">{doc.title}</span><span className="mt-1 block text-[12px] text-[#78857f]">{doc.description}</span></span>
                <span className="font-mono text-[10px] uppercase tracking-[0.12em] text-[#6d7a74] transition-colors group-hover:text-lime">{doc.readTime} →</span>
              </Link>
            ))}
          </div>
        </section>

        <div className="mt-16 flex items-center justify-between border-t border-white/10 pt-6 font-mono text-[10px] uppercase tracking-[0.13em] text-[#697670]">
          <span>Next: Core model</span>
          <Link className="flex items-center gap-2 text-lime transition-colors hover:text-white" href="/docs/core-model">Open chapter <ArrowUpRight className="h-3.5 w-3.5" /></Link>
        </div>
      </main>
    </DocsFrame>
  );
}
