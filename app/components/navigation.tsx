import Link from "next/link";
import { chapterDocs, docs, type DocPage, type SearchEntry } from "@/app/lib/spec";
import { MobileNavigation, SearchDialog, ThemeSwitcher } from "./client-controls";
import { ArrowIcon } from "./icons";

export function DraftBadge({ draft }: { draft: string }) {
  return <span className="draft-badge"><i />{draft}</span>;
}

export function SiteHeader({ searchEntries, draft }: { searchEntries: SearchEntry[]; draft: string }) {
  return <header className="site-header">
    <Link aria-label="Kond ドキュメント ホーム" className="brand" href="/"><span className="brand-mark">K</span><span>Kond<small>LANGUAGE DOCUMENTATION</small></span></Link>
    <nav aria-label="メインナビゲーション">
      <Link href="/docs/specification-ja/">仕様</Link>
      <Link href="/overview/">言語概要</Link>
      <Link href="/playground/">Examples</Link>
      <Link href="/diagnostics/">診断</Link>
    </nav>
    <div className="header-actions"><SearchDialog entries={searchEntries} /><ThemeSwitcher /><DraftBadge draft={draft} /></div>
  </header>;
}

export function DocsSidebar({ current }: { current?: string }) {
  const special = docs.slice(0, 5);
  const groups = [
    { label: "Foundations", pages: chapterDocs.slice(0, 4) },
    { label: "State & resources", pages: chapterDocs.slice(4, 8) },
    { label: "Proof & security", pages: chapterDocs.slice(8, 13) },
    { label: "Reference", pages: chapterDocs.slice(13) }
  ];
  return <nav aria-label="仕様書の章" className="docs-sidebar">
    <div className="sidebar-group"><span>Specification</span>{special.map((doc) => <SidebarLink current={current} doc={doc} key={doc.slug} />)}</div>
    {groups.map((group) => <div className="sidebar-group" key={group.label}><span>{group.label}</span>{group.pages.map((doc) => <SidebarLink current={current} doc={doc} key={doc.slug} />)}</div>)}
  </nav>;
}

function SidebarLink({ doc, current }: { doc: DocPage; current?: string }) {
  return <Link aria-current={current === doc.slug ? "page" : undefined} className={current === doc.slug ? "active" : ""} href={`/docs/${doc.slug}/`}>{doc.shortTitle}</Link>;
}

export function TableOfContents({ doc }: { doc: DocPage }) {
  const headings = doc.headings.filter((heading) => heading.depth >= 2 && heading.depth <= 3);
  return <nav aria-label="このページの目次" className="toc"><span>ON THIS PAGE</span>{headings.map((heading) => <a className={heading.depth === 3 ? "nested" : ""} href={`#${heading.id}`} key={heading.id}>{heading.text}</a>)}<a className="source-link" href={`https://github.com/Kicky1618/kond-lang/blob/main/${doc.source}`} rel="noreferrer noopener" target="_blank">Source file ↗</a></nav>;
}

export function DocsBreadcrumbs({ title }: { title: string }) {
  return <nav aria-label="パンくず" className="breadcrumbs"><Link href="/">Kond</Link><span>/</span><Link href="/docs/specification-ja/">Docs</Link><span>/</span><span>{title}</span></nav>;
}

export function LanguageSwitcher({ language }: { language: "ja" | "en" }) {
  return <div aria-label="言語切り替え" className="language-switcher"><Link className={language === "ja" ? "active" : ""} href="/docs/specification-ja/" hrefLang="ja">日本語</Link><Link className={language === "en" ? "active" : ""} href="/docs/specification-en/" hrefLang="en">English</Link></div>;
}

export function PreviousNextNavigation({ doc }: { doc: DocPage }) {
  const position = docs.findIndex((item) => item.slug === doc.slug);
  const previous = docs[position - 1];
  const next = docs[position + 1];
  return <nav aria-label="前後のページ" className="prev-next">
    {previous ? <Link href={`/docs/${previous.slug}/`}><span>← PREVIOUS</span><strong>{previous.shortTitle}</strong></Link> : <i />}
    {next ? <Link className="next" href={`/docs/${next.slug}/`}><span>NEXT →</span><strong>{next.shortTitle}</strong></Link> : <i />}
  </nav>;
}

export function DocsShell({ children, current }: { children: React.ReactNode; current?: string }) {
  return <div className="docs-shell"><aside className="desktop-sidebar"><DocsSidebar current={current} /></aside><MobileNavigation><DocsSidebar current={current} /></MobileNavigation>{children}</div>;
}

export function Footer() {
  return <footer><div><span className="brand-mark">K</span><p><strong>Kond Language Documentation</strong><small>All normative content is sourced from the repository specification.</small></p></div><Link href="/docs/changelog/">Changelog <ArrowIcon /></Link></footer>;
}
