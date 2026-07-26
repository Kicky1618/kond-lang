import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { Markdown } from "@/app/components/markdown";
import { DocsBreadcrumbs, DocsShell, LanguageSwitcher, PreviousNextNavigation, TableOfContents } from "@/app/components/navigation";
import { docs, getDoc } from "@/app/lib/spec";

export function generateStaticParams() {
  return docs.map((doc) => ({ slug: doc.slug }));
}

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }): Promise<Metadata> {
  const { slug } = await params;
  const doc = getDoc(slug);
  if (!doc) return {};
  return {
    title: doc.shortTitle,
    description: doc.description,
    alternates: { canonical: `https://kicky1618.github.io/kond-lang/docs/${doc.slug}/` },
    openGraph: { title: `${doc.shortTitle} — Kond`, description: doc.description }
  };
}

export default async function DocPage({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const doc = getDoc(slug);
  if (!doc) notFound();
  const isSpecification = slug === "specification-ja" || slug === "specification-en";
  return <main id="main">
    <DocsShell current={slug}>
      <article className="doc-article">
        <DocsBreadcrumbs title={doc.shortTitle} />
        <div className="doc-source-row"><span>NORMATIVE SOURCE · {doc.sourceLabel}</span>{isSpecification && <LanguageSwitcher language={doc.language} />}</div>
        <Markdown source={doc.body} />
        <PreviousNextNavigation doc={doc} />
      </article>
      <aside className="toc-column"><TableOfContents doc={doc} /></aside>
    </DocsShell>
  </main>;
}
