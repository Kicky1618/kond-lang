import Link from "next/link";
import { BrandMark } from "./site-header";
import { PageContainer } from "./ui";

export default function SiteFooter() {
  return (
    <footer className="border-t border-white/10">
      <PageContainer className="flex flex-col gap-6 py-8 text-smoke md:flex-row md:items-center md:justify-between">
        <div className="flex items-center gap-3">
          <BrandMark />
          <span className="font-mono text-[12px] text-white">kond / experimental language</span>
        </div>
        <div className="flex flex-wrap items-center gap-x-6 gap-y-2 font-mono text-[10px] uppercase tracking-[0.14em]">
          <Link className="transition-colors hover:text-lime" href="/docs">Specification</Link>
          <Link className="transition-colors hover:text-lime" href="/#capabilities">Capabilities</Link>
          <Link className="transition-colors hover:text-lime" href="/#start">Quick start</Link>
          <span>© 2026 Kond</span>
        </div>
      </PageContainer>
    </footer>
  );
}
