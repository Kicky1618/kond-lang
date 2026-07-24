import Link from "next/link";
import SiteFooter from "./components/site-footer";
import SiteHeader from "./components/site-header";
import { CodeWindow } from "./components/code-lines";
import { ArrowUpRight, ChevronRight } from "./components/icons";
import { Eyebrow, PageContainer, ProofBadge } from "./components/ui";

const heroSnippet = [
  "let order where self is Object = input()",
  'check order has "items"',
  "let total = move order.items // proof travels with the value",
  "",
  "if total > 0 {",
  "  require total < 100_000",
  "  save(total)",
  "}",
  "",
  "// verified: 0 unknown conditions",
];

const principles = [
  {
    index: "01",
    title: "Every value starts\nas Any.",
    description: "No ceremony before you need it. Add knowledge as you prove it, and let the program carry that evidence forward.",
    accent: "bg-lime",
    mark: "A",
  },
  {
    index: "02",
    title: "Mutation keeps\nits promise.",
    description: "Invariants are not comments. Updates are transactional, and stale proofs cannot quietly survive a new value version.",
    accent: "bg-cyan",
    mark: "↻",
  },
  {
    index: "03",
    title: "Speed follows\nproof.",
    description: "The optimizer only rewrites what it can justify. Exact, observable semantics stay in charge of the fast path.",
    accent: "bg-[#f7ce89]",
    mark: "→",
  },
];

export default function HomePage() {
  return (
    <main className="overflow-hidden bg-ink">
      <section className="relative min-h-[720px] border-b border-white/10">
        <div className="page-grid pointer-events-none absolute inset-0 opacity-70" />
        <div className="hero-glow pointer-events-none absolute inset-0" />
        <div className="noise absolute inset-0 opacity-[0.045]" />
        <SiteHeader />

        <PageContainer className="relative z-10 grid gap-14 pb-22 pt-18 md:pt-24 lg:grid-cols-[minmax(0,0.92fr)_minmax(480px,1.08fr)] lg:items-center lg:gap-18 lg:pb-28">
          <div className="max-w-[600px]">
            <div className="mb-7 flex items-center gap-3">
              <span className="h-px w-9 bg-lime" />
              <Eyebrow>A language for verified change</Eyebrow>
            </div>
            <h1 className="max-w-[680px] text-[clamp(3.6rem,8vw,7.3rem)] font-650 leading-[0.91] tracking-[-0.085em] text-white">
              Code with
              <br />
              <span className="text-lime">proof.</span>
            </h1>
            <p className="mt-8 max-w-[500px] text-[17px] leading-[1.75] tracking-[-0.01em] text-[#aeb8b4] sm:text-[19px]">
              Kond は、値・条件・所有権をひとつの語彙で扱う実験的な言語。安全性を後付けするのではなく、コードが最初から知識を持って動きます。
            </p>
            <div className="mt-9 flex flex-wrap gap-3">
              <Link className="button-primary" href="/docs">
                仕様を読む
                <ArrowUpRight className="h-4 w-4" />
              </Link>
              <Link className="button-secondary" href="#quickstart">
                60 秒で試す
                <ChevronRight className="h-4 w-4" />
              </Link>
            </div>
            <div className="mt-12 flex flex-wrap items-center gap-x-6 gap-y-3 font-mono text-[10px] uppercase tracking-[0.13em] text-[#6e7a77]">
              <span className="flex items-center gap-2"><span className="h-1.5 w-1.5 rounded-full bg-lime" />C++17 runtime</span>
              <span>Draft 0.2</span>
              <span>Safe by design</span>
            </div>
          </div>

          <div className="relative mx-auto w-full max-w-[650px] lg:pt-5">
            <div className="absolute -inset-12 rounded-full bg-lime/6 blur-3xl" />
            <CodeWindow
              bodyClassName="px-4 py-6 text-[12px] sm:px-7 sm:py-8 sm:text-[13px]"
              code={heroSnippet}
              footer="0.8ms / 4 proofs"
            />
            <div className="absolute -bottom-6 -left-3 flex items-center gap-3 rounded-[12px] border border-white/12 bg-[#171d21] px-4 py-3 shadow-xl sm:-left-7">
              <span className="flex h-8 w-8 items-center justify-center rounded-full bg-lime/12 font-mono text-[13px] text-lime">✓</span>
              <div>
                <p className="font-mono text-[9px] uppercase tracking-[0.14em] text-[#72807b]">proof state</p>
                <p className="mt-0.5 text-[12px] font-650 text-white">Holds&lt;TotalBounded&gt;</p>
              </div>
            </div>
          </div>
        </PageContainer>
        <div className="pointer-events-none absolute bottom-0 left-1/2 hidden h-22 w-px bg-gradient-to-b from-transparent via-lime/50 to-lime/0 lg:block" />
      </section>

      <section className="relative border-b border-white/10 bg-[#0d1217]" id="principles">
        <PageContainer className="py-22 lg:py-28">
          <div className="grid gap-10 lg:grid-cols-[0.75fr_1.25fr] lg:gap-20">
            <div>
              <Eyebrow>01 / The premise</Eyebrow>
              <h2 className="mt-5 max-w-[380px] text-[clamp(2.3rem,4.8vw,4.5rem)] font-650 leading-[0.98] tracking-[-0.075em] text-white">
                安全性は、<br /><span className="text-lime">流れ</span>になる。
              </h2>
              <p className="mt-7 max-w-[360px] text-[15px] leading-[1.85] text-[#899591]">
                Kond は型を増やすことで複雑さを隠しません。値が何を知っているか、何を保証されているかを、実行の流れそのものに残します。
              </p>
            </div>
            <div className="grid gap-px overflow-hidden rounded-[18px] border border-white/10 bg-white/10 md:grid-cols-3">
              {principles.map((principle) => (
                <article className="group relative bg-[#11171c] p-6 transition-colors duration-300 hover:bg-[#151d21] sm:p-7" key={principle.index}>
                  <div className="flex items-start justify-between">
                    <span className="font-mono text-[10px] tracking-[0.16em] text-[#66736e]">{principle.index}</span>
                    <span className={`flex h-9 w-9 items-center justify-center rounded-[10px] text-[15px] font-700 text-ink ${principle.accent}`}>{principle.mark}</span>
                  </div>
                  <h3 className="mt-18 whitespace-pre-line text-[22px] font-650 leading-[1.1] tracking-[-0.055em] text-white">{principle.title}</h3>
                  <p className="mt-5 text-[13px] leading-[1.8] text-[#87938f]">{principle.description}</p>
                  <span className="absolute bottom-0 left-0 h-0.5 w-0 bg-lime transition-all duration-500 group-hover:w-full" />
                </article>
              ))}
            </div>
          </div>
        </PageContainer>
      </section>

      <section className="border-b border-white/10 bg-paper text-ink" id="quickstart">
        <PageContainer className="py-22 lg:py-28">
          <div className="flex flex-col justify-between gap-8 md:flex-row md:items-end">
            <div>
              <p className="font-mono text-[10px] uppercase tracking-[0.18em] text-[#658d2b]">02 / The loop</p>
              <h2 className="mt-5 max-w-[620px] text-[clamp(2.4rem,5vw,5rem)] font-650 leading-[0.96] tracking-[-0.08em]">知識が、コードを<br /><span className="text-[#658d2b]">前に進める。</span></h2>
            </div>
            <p className="max-w-[320px] text-[14px] leading-[1.8] text-[#5f6864]">値を受け取り、証明し、変え、また証明する。その反復が Kond の基本リズムです。</p>
          </div>

          <div className="mt-16 grid gap-7 lg:grid-cols-[1.15fr_0.85fr]">
            <div className="overflow-hidden rounded-[18px] bg-ink p-5 text-white sm:p-7">
              <div className="mb-8 flex items-center justify-between border-b border-white/10 pb-4">
                <div className="flex items-center gap-3">
                  <span className="flex h-8 w-8 items-center justify-center rounded-full border border-lime/40 font-mono text-[11px] text-lime">01</span>
                  <span className="text-[13px] font-650">condition pipeline</span>
                </div>
                <span className="font-mono text-[10px] text-[#77837e]">runtime / compile-time</span>
              </div>
              <div className="grid gap-0 sm:grid-cols-4">
                {[
                  ["input", "Any", "#72807b"],
                  ["check", "Proof", "#c8ff70"],
                  ["mutate", "Version", "#80e7e9"],
                  ["rewrite", "Fast", "#f7ce89"],
                ].map(([label, value, color], index) => (
                  <div className="relative flex gap-3 pb-8 sm:block sm:pb-0" key={label}>
                    <div className="relative z-1 flex h-11 w-11 shrink-0 items-center justify-center rounded-[12px] border border-white/12 bg-[#161d21] font-mono text-[11px]" style={{ color }}>
                      0{index + 1}
                    </div>
                    <div className="pt-0 sm:pt-5">
                      <p className="font-mono text-[10px] uppercase tracking-[0.14em] text-[#6e7a76]">{label}</p>
                      <p className="mt-1 text-[14px] font-650" style={{ color }}>{value}</p>
                    </div>
                    {index < 3 && <span className="absolute left-[22px] top-11 h-[calc(100%-1.25rem)] w-px bg-white/15 sm:left-11 sm:top-[22px] sm:h-px sm:w-[calc(100%-1.5rem)]" />}
                  </div>
                ))}
              </div>
              <div className="mt-10 rounded-[10px] border border-lime/12 bg-lime/5 px-4 py-3 font-mono text-[11px] text-[#bfd998]">
                <span className="mr-2 text-lime">$</span> kond check examples/order_processing.kd <span className="ml-2 text-[#75836f]">→ verified</span>
              </div>
            </div>
            <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-1">
              <div className="rounded-[18px] border border-ink/10 bg-white p-6 sm:p-7">
                <ProofBadge tone="cyan">first-class conditions</ProofBadge>
                <p className="mt-6 max-w-[390px] text-[17px] font-650 leading-[1.35] tracking-[-0.035em]">証明は一度きりの assert ではなく、値のバージョンに束縛された証拠。</p>
              </div>
              <div className="rounded-[18px] border border-ink/10 bg-[#e7f0e4] p-6 sm:p-7">
                <ProofBadge>deterministic ownership</ProofBadge>
                <p className="mt-6 max-w-[390px] text-[17px] font-650 leading-[1.35] tracking-[-0.035em]">move と borrow の履歴を、決定的なプリフライトで追跡。</p>
              </div>
            </div>
          </div>
        </PageContainer>
      </section>

      <section className="relative border-b border-white/10 bg-ink" id="docs-preview">
        <div className="page-grid pointer-events-none absolute inset-0 opacity-40" />
        <PageContainer className="relative py-22 lg:py-28">
          <div className="flex flex-col justify-between gap-6 md:flex-row md:items-end">
            <div>
              <Eyebrow>03 / Read the language</Eyebrow>
              <h2 className="mt-5 text-[clamp(2.5rem,5vw,5rem)] font-650 leading-[0.95] tracking-[-0.08em] text-white">仕様書を、<span className="text-lime">地図</span>にする。</h2>
            </div>
            <Link className="group inline-flex items-center gap-2 font-mono text-[11px] uppercase tracking-[0.14em] text-lime" href="/docs">
              Browse all docs
              <ArrowUpRight className="h-3.5 w-3.5 transition-transform group-hover:translate-x-0.5 group-hover:-translate-y-0.5" />
            </Link>
          </div>
          <div className="mt-14 grid gap-4 md:grid-cols-3">
            <Link className="group rounded-[18px] border border-white/12 bg-white/[0.035] p-6 transition-all duration-300 hover:-translate-y-1 hover:border-lime/45 hover:bg-lime/[0.06] sm:p-7" href="/docs/core-model">
              <span className="font-mono text-[10px] text-lime">01 / FUNDAMENTALS</span>
              <h3 className="mt-16 text-[22px] font-650 tracking-[-0.055em] text-white">Core model</h3>
              <p className="mt-3 text-[13px] leading-[1.75] text-[#84918d]">Any、Condition、証明状態。Kond が世界をどう分けて考えるか。</p>
              <span className="mt-7 inline-flex items-center gap-2 font-mono text-[10px] uppercase tracking-[0.12em] text-[#78847f] transition-colors group-hover:text-lime">Start here <ChevronRight className="h-3 w-3" /></span>
            </Link>
            <Link className="group rounded-[18px] border border-white/12 bg-white/[0.035] p-6 transition-all duration-300 hover:-translate-y-1 hover:border-cyan/45 hover:bg-cyan/[0.05] sm:p-7" href="/docs/ownership">
              <span className="font-mono text-[10px] text-cyan">05 / RESOURCES</span>
              <h3 className="mt-16 text-[22px] font-650 tracking-[-0.055em] text-white">Ownership &amp; borrow</h3>
              <p className="mt-3 text-[13px] leading-[1.75] text-[#84918d]">リソースの移動と借用を、決定的な証明として扱うためのモデル。</p>
              <span className="mt-7 inline-flex items-center gap-2 font-mono text-[10px] uppercase tracking-[0.12em] text-[#78847f] transition-colors group-hover:text-cyan">Go deeper <ChevronRight className="h-3 w-3" /></span>
            </Link>
            <Link className="group rounded-[18px] border border-white/12 bg-white/[0.035] p-6 transition-all duration-300 hover:-translate-y-1 hover:border-[#f7ce89]/45 hover:bg-[#f7ce89]/[0.05] sm:p-7" href="/docs/optimization">
              <span className="font-mono text-[10px] text-[#f7ce89]">09 / PERFORMANCE</span>
              <h3 className="mt-16 text-[22px] font-650 tracking-[-0.055em] text-white">Optimization</h3>
              <p className="mt-3 text-[13px] leading-[1.75] text-[#84918d]">証明できる書き換えだけを許す、意味論と速度の契約。</p>
              <span className="mt-7 inline-flex items-center gap-2 font-mono text-[10px] uppercase tracking-[0.12em] text-[#78847f] transition-colors group-hover:text-[#f7ce89]">See the rules <ChevronRight className="h-3 w-3" /></span>
            </Link>
          </div>
        </PageContainer>
      </section>

      <section className="bg-[#151d1b]">
        <PageContainer className="flex flex-col gap-9 py-18 md:flex-row md:items-center md:justify-between lg:py-22">
          <div>
            <Eyebrow>The short version</Eyebrow>
            <p className="mt-4 max-w-[670px] text-[clamp(1.35rem,2.7vw,2.35rem)] font-550 leading-[1.2] tracking-[-0.055em] text-white">“A program is a transformation of values <span className="text-lime">and knowledge.</span>”</p>
          </div>
          <Link className="button-primary shrink-0 self-start md:self-auto" href="/docs">
            Kond を知る
            <ArrowUpRight className="h-4 w-4" />
          </Link>
        </PageContainer>
      </section>
      <SiteFooter />
    </main>
  );
}
