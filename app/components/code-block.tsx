import type { CodeLang } from "../lib/highlight";
import { CodeWindow } from "./code-lines";

const LANG_META: Record<CodeLang, {
  badge: string;
  status: string;
  title: string;
}> = {
  kond: { badge: "safe", status: "all checks passed", title: "main.kd" },
  console: { badge: "output", status: "compiler response", title: "output" },
};

export default function CodeBlock({
  code,
  lang = "kond",
  title,
  className = "",
}: {
  code: string;
  lang?: CodeLang;
  title?: string;
  className?: string;
}) {
  const lines = code.split("\n");
  const meta = LANG_META[lang];

  return (
    <CodeWindow
      badge={meta.badge}
      bodyClassName="py-4 text-[11.5px] leading-[1.9] sm:py-5 sm:text-[12.5px]"
      className={`doc-code-block ${className}`}
      code={lines}
      copyable
      footer={`${lines.length} lines`}
      lang={lang}
      status={meta.status}
      title={title ?? meta.title}
    />
  );
}
