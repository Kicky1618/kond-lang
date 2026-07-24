import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: {
    default: "Kond — Code with proof",
    template: "%s — Kond",
  },
  description:
    "Kond is an experimental language for code that carries its conditions, ownership, and security guarantees.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="ja">
      <body>{children}</body>
    </html>
  );
}
