import type { Metadata } from "next";
import "./globals.css";
import { buildSearchIndex, getDraft } from "./lib/spec";
import { Footer, SiteHeader } from "./components/navigation";

export const metadata: Metadata = {
  metadataBase: new URL("https://kicky1618.github.io/kond-lang"),
  title: { default: "Kond Language Documentation", template: "%s — Kond" },
  description: "Any、Condition、Proof を中心に設計された実験的プログラミング言語 Kond の公式仕様ドキュメント。",
  openGraph: {
    title: "Kond Language Documentation",
    description: "Value is Any. Knowledge is evidence.",
    type: "website",
    locale: "ja_JP"
  },
  alternates: { canonical: "https://kicky1618.github.io/kond-lang/" }
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  const index = buildSearchIndex();
  const draft = getDraft();
  return <html lang="ja" suppressHydrationWarning>
    <body><a className="skip-link" href="#main">本文へ移動</a><SiteHeader draft={draft} searchEntries={index} />{children}<Footer /></body>
  </html>;
}
