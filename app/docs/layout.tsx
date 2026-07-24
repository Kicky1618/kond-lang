import type { Metadata } from "next";
import SiteHeader from "../components/site-header";
import { Eyebrow, PageContainer, Pill } from "../components/ui";

export const metadata: Metadata = {
  title: "Documentation",
  description: "Kond Draft 0.2 の設計思想と仕様を読む。",
};

export default function DocsLayout({ children }: { children: React.ReactNode }) {
  return (
    <div className="min-h-screen overflow-x-clip bg-ink">
      <div className="relative border-b border-white/10 bg-[#0d1217]">
        <div className="page-grid pointer-events-none absolute inset-0 opacity-45" />
        <SiteHeader />
        <PageContainer className="relative pb-10 pt-8 lg:pb-12">
          <div className="flex items-center gap-3">
            <span className="h-px w-8 bg-lime" />
            <Eyebrow>Kond / Documentation</Eyebrow>
          </div>
          <div className="mt-5 flex flex-col justify-between gap-6 lg:flex-row lg:items-end">
            <div>
              <h1 className="max-w-[720px] text-[clamp(2.5rem,5vw,4.8rem)] font-650 leading-[0.95] tracking-[-0.08em] text-white">仕様を、<span className="text-lime">順番に。</span></h1>
              <p className="mt-5 max-w-[600px] text-[14px] leading-[1.8] text-[#9aa6a2] sm:text-[15px]">コアモデルから最適化まで。読む順序と現在地がわかる、Kond の設計図です。</p>
            </div>
            <div className="flex shrink-0 gap-2 font-mono text-[10px] uppercase tracking-[0.13em] text-[#77847f]">
              <Pill className="px-3 py-2 text-[10px] tracking-[0.13em]">Draft 0.2</Pill>
              <Pill className="px-3 py-2 text-[10px] tracking-[0.13em]" tone="neutral">ja / en</Pill>
            </div>
          </div>
        </PageContainer>
      </div>
      {children}
    </div>
  );
}
