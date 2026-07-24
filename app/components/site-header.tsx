import Link from "next/link";
import { ArrowUpRight } from "./icons";
import { PageContainer } from "./ui";

export function BrandMark() {
  return (
    <span aria-hidden="true" className="relative inline-flex h-7 w-7 items-center justify-center rounded-[9px] bg-lime text-[13px] font-800 tracking-[-0.12em] text-ink">
      <span className="relative -left-[1px]">K</span>
      <span className="relative -left-[2px] opacity-55">.</span>
    </span>
  );
}

export default function SiteHeader() {
  return (
    <PageContainer as="header" className="relative z-20 flex items-center justify-between py-5">
      <Link aria-label="Kond ホーム" className="group flex items-center gap-3" href="/">
        <BrandMark />
        <span className="font-mono text-[15px] font-700 tracking-[-0.04em] text-white">kond</span>
        <span className="hidden rounded-full border border-white/12 px-2 py-1 font-mono text-[9px] uppercase tracking-[0.14em] text-smoke sm:inline-flex">
          draft 0.2
        </span>
      </Link>

      <nav aria-label="メインナビゲーション" className="hidden items-center gap-8 text-[13px] text-smoke md:flex">
        <Link className="transition-colors hover:text-white" href="/#principles">Principles</Link>
        <Link className="transition-colors hover:text-white" href="/docs">Docs</Link>
        <Link className="transition-colors hover:text-white" href="/#quickstart">Quick start</Link>
      </nav>

      <Link className="group hidden items-center gap-2 font-mono text-[11px] uppercase tracking-[0.12em] text-lime sm:flex" href="/docs">
        Read the spec
        <ArrowUpRight className="h-3.5 w-3.5 transition-transform group-hover:translate-x-0.5 group-hover:-translate-y-0.5" />
      </Link>
      <Link aria-label="ドキュメント" className="font-mono text-[11px] uppercase tracking-[0.12em] text-lime sm:hidden" href="/docs">docs ↗</Link>
    </PageContainer>
  );
}
