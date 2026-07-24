import type { Metadata } from "next";
import Link from "next/link";
import { notFound } from "next/navigation";
import CodeBlock from "../../components/code-block";
import DocsFrame from "../../components/docs-frame";
import { ArrowUpRight } from "../../components/icons";
import { Breadcrumbs, Pill } from "../../components/ui";
import { docs, getDoc, sectionId } from "../../lib/docs";

function Inline({ text }: { text: string }) {
  return (
    <>
      {text.split(/(`[^`]+`)/g).map((part, index) => part.startsWith("`") && part.endsWith("`") ? <code key={index}>{part.slice(1, -1)}</code> : part)}
    </>
  );
}

function RichText({ text }: { text: string }) {
  return (
    <p>
      <Inline text={text} />
    </p>
  );
}

export function generateStaticParams() {
  return docs.map((doc) => ({ slug: doc.slug }));
}

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }): Promise<Metadata> {
  const { slug } = await params;
  const doc = getDoc(slug);
  return { title: doc?.title ?? "Documentation" };
}

export default async function DocPage({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = getDoc(slug);
  if (!doc) notFound();

  const currentIndex = docs.findIndex((item) => item.slug === doc.slug);
  const previous = currentIndex > 0 ? docs[currentIndex - 1] : undefined;
  const next = currentIndex < docs.length - 1 ? docs[currentIndex + 1] : undefined;

  return (
    <DocsFrame currentSlug={doc.slug}>
      <main className="min-w-0" id="top">
        <Breadcrumbs items={[
          { href: "/docs", label: "Docs" },
          { label: doc.category },
          { active: true, label: doc.number },
        ]} />

        <article className="prose-kond mt-8">
          <div className="flex flex-wrap items-center gap-3">
            <Pill className="px-3 py-1.5 text-[10px] tracking-[0.13em]" tone={doc.color}>{doc.number} / {doc.category}</Pill>
            <span className="font-mono text-[10px] uppercase tracking-[0.13em] text-[#697670]">{doc.readTime}</span>
          </div>
          <h1 className="mt-6 text-[clamp(2.7rem,6vw,5.6rem)] font-650 leading-[0.94] tracking-[-0.085em] text-white">{doc.title}</h1>
          <p className="mt-6 max-w-[690px] text-[clamp(1.05rem,2vw,1.3rem)] leading-[1.65] text-[#c3cdc8]">{doc.description}</p>

          <div className="my-12 h-px bg-gradient-to-r from-lime/60 via-white/15 to-transparent" />

          <nav aria-label="この章の目次" className="mb-10 rounded-[14px] border border-white/10 bg-white/[0.025] p-4 sm:p-5 xl:hidden">
            <p className="font-mono text-[9px] uppercase tracking-[0.16em] text-[#68756f]">In this chapter</p>
            <ol className="mt-3 grid gap-2 sm:grid-cols-2">
              {doc.sections.map((section, index) => (
                <li key={section.heading}>
                  <a className="flex items-center gap-2 text-[12px] text-[#aeb8b4] transition-colors hover:text-lime" href={`#${sectionId(section.heading)}`}>
                    <span className="font-mono text-[9px] text-lime">{String(index + 1).padStart(2, "0")}</span>
                    {section.heading}
                  </a>
                </li>
              ))}
            </ol>
          </nav>

          {doc.sections.map((section, index) => (
            <section className="doc-section" id={sectionId(section.heading)} key={section.heading}>
              <div className="section-kicker">Section {String(index + 1).padStart(2, "0")}</div>
              <h2>{section.heading}</h2>
              {section.paragraphs.map((paragraph) => <RichText key={paragraph} text={paragraph} />)}
              {section.code && <CodeBlock className="my-7" code={section.code} lang={section.codeLang ?? "kond"} title={section.codeTitle} />}
              {section.bullets && <ul className="my-5 space-y-3 pl-5 text-[14px] leading-[1.75] text-[#aab5b0] marker:text-lime">{section.bullets.map((bullet) => <li key={bullet}><Inline text={bullet} /></li>)}</ul>}
              {section.note && <aside className="my-7 overflow-hidden rounded-[12px] border border-lime/16 bg-lime/[0.045] text-[13px] leading-[1.8] text-[#c0d2b1]"><div className="border-b border-lime/12 px-5 py-2 font-mono text-[9px] uppercase tracking-[0.15em] text-lime">Key idea</div><div className="px-5 py-4"><Inline text={section.note} /></div></aside>}
            </section>
          ))}

          <div className="mt-18 rounded-[14px] border border-white/10 bg-[#11171c] p-5 sm:p-6">
            <p className="font-mono text-[10px] uppercase tracking-[0.15em] text-[#65726d]">In one line</p>
            <p className="mt-3 text-[16px] leading-[1.6] text-white">{doc.title} is where Kond turns a claim into an explicit, checkable part of the program.</p>
          </div>
        </article>

        <div className="mt-16 grid gap-3 border-t border-white/10 pt-6 sm:grid-cols-2">
          {previous ? <Link className="group rounded-[12px] border border-white/10 p-4 transition-colors hover:border-white/25 hover:bg-white/[0.025]" href={`/docs/${previous.slug}`}><span className="font-mono text-[9px] uppercase tracking-[0.13em] text-[#66736e]">← Previous</span><span className="mt-3 block text-[14px] font-600 text-white group-hover:text-lime">{previous.title}</span></Link> : <div />}
          {next && <Link className="group rounded-[12px] border border-white/10 p-4 text-left transition-colors hover:border-lime/35 hover:bg-white/[0.025] sm:text-right" href={`/docs/${next.slug}`}><span className="font-mono text-[9px] uppercase tracking-[0.13em] text-[#66736e]">Next →</span><span className="mt-3 block text-[14px] font-600 text-white group-hover:text-lime">{next.title}</span></Link>}
        </div>
        <Link className="mt-10 flex items-center gap-2 font-mono text-[10px] uppercase tracking-[0.14em] text-lime" href="/docs">Back to overview <ArrowUpRight className="h-3.5 w-3.5" /></Link>
      </main>
    </DocsFrame>
  );
}
