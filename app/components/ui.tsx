import Link from "next/link";
import { ChevronRight } from "./icons";

type Tone = "lime" | "cyan" | "amber" | "neutral";

const toneClasses: Record<Tone, string> = {
  lime: "border-lime/25 bg-lime/7 text-lime",
  cyan: "border-cyan/25 bg-cyan/7 text-cyan",
  amber: "border-[#f7ce89]/25 bg-[#f7ce89]/7 text-[#f7ce89]",
  neutral: "border-white/10 bg-white/[0.025] text-[#9aa6a2]",
};

export function PageContainer({
  as: Element = "div",
  children,
  className = "",
}: {
  as?: "div" | "section" | "header" | "footer";
  children: React.ReactNode;
  className?: string;
}) {
  return <Element className={`mx-auto w-full max-w-[1240px] px-5 sm:px-8 lg:px-10 ${className}`}>{children}</Element>;
}

export function Eyebrow({
  children,
  className = "",
}: {
  children: React.ReactNode;
  className?: string;
}) {
  return <p className={`eyebrow ${className}`}>{children}</p>;
}

export function Pill({
  children,
  className = "",
  tone = "lime",
}: {
  children: React.ReactNode;
  className?: string;
  tone?: Tone;
}) {
  return (
    <span className={`inline-flex items-center rounded-full border px-2.5 py-1 font-mono text-[9px] uppercase tracking-[0.12em] ${toneClasses[tone]} ${className}`}>
      {children}
    </span>
  );
}

export function ProofBadge({
  children,
  tone = "lime",
}: {
  children: React.ReactNode;
  tone?: "lime" | "cyan";
}) {
  return (
    <Pill className="gap-2 px-3 py-1.5 text-[10px] tracking-[0.1em]" tone={tone}>
      <span className={`h-1.5 w-1.5 rounded-full ${tone === "cyan" ? "bg-cyan" : "bg-lime"}`} />
      {children}
    </Pill>
  );
}

export function Breadcrumbs({
  items,
}: {
  items: Array<{ href?: string; label: React.ReactNode; active?: boolean }>;
}) {
  return (
    <nav aria-label="パンくずリスト" className="flex flex-wrap items-center gap-2 font-mono text-[10px] uppercase tracking-[0.14em] text-[#64716c]">
      {items.map((item, index) => (
        <span className="contents" key={index}>
          {index > 0 && <ChevronRight className="h-3 w-3" />}
          {item.href ? (
            <Link className="transition-colors hover:text-white" href={item.href}>{item.label}</Link>
          ) : (
            <span className={item.active ? "text-lime" : ""}>{item.label}</span>
          )}
        </span>
      ))}
    </nav>
  );
}
