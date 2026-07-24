import { highlightLine, type CodeLang } from "../lib/highlight";
import CopyButton from "./copy-button";

export function CodeLines({
  code,
  lang = "kond",
}: {
  code: string | string[];
  lang?: CodeLang;
}) {
  const lines = Array.isArray(code) ? code : code.split("\n");

  return (
    <>
      {lines.map((line, index) => (
        <div className="code-line" data-line={String(index + 1).padStart(2, "0")} key={`${index}-${line}`}>
          <code>{highlightLine(line, lang, index)}</code>
        </div>
      ))}
    </>
  );
}

export function WindowDots() {
  return (
    <span aria-hidden="true" className="flex items-center gap-2">
      <span className="h-2.5 w-2.5 rounded-full bg-[#ff6e63]/80" />
      <span className="h-2.5 w-2.5 rounded-full bg-[#f7ce89]/80" />
      <span className="h-2.5 w-2.5 rounded-full bg-lime/80" />
    </span>
  );
}

export function CodeWindow({
  badge = "safe",
  bodyClassName = "",
  className = "",
  code,
  copyable = false,
  footer,
  lang = "kond",
  status = "all checks passed",
  title = "main.kd",
}: {
  badge?: string;
  bodyClassName?: string;
  className?: string;
  code: string | string[];
  copyable?: boolean;
  footer?: string;
  lang?: CodeLang;
  status?: string;
  title?: string;
}) {
  const source = Array.isArray(code) ? code.join("\n") : code;
  const isConsole = lang === "console";

  return (
    <figure className={`code-window relative overflow-hidden rounded-[18px] border border-white/14 bg-[#11161b]/95 ${className}`}>
      <figcaption className="flex min-h-13 items-center justify-between border-b border-white/10 px-4 py-3 sm:px-5">
        <WindowDots />
        <div className="flex min-w-0 items-center gap-3 font-mono text-[9px] text-[#72807b] sm:gap-4 sm:text-[10px]">
          <span className="max-w-36 truncate text-white/80 sm:max-w-64">{title}</span>
          <span>{badge}</span>
          {copyable && <CopyButton text={source} />}
        </div>
      </figcaption>
      <div className={`code-scroll overflow-x-auto font-mono text-[#d5dfd8] ${bodyClassName}`}>
        <CodeLines code={code} lang={lang} />
      </div>
      <div className="relative flex items-center justify-between border-t border-white/10 bg-[#0c1115] px-4 py-3 font-mono text-[9px] sm:px-5">
        <span className={`flex items-center gap-2 ${isConsole ? "text-cyan" : "text-lime"}`}>
          <span className={`h-1.5 w-1.5 rounded-full ${isConsole ? "bg-cyan shadow-[0_0_10px_#80e7e9]" : "bg-lime shadow-[0_0_10px_#c8ff70]"}`} />
          {status}
        </span>
        {footer && <span className="text-[#687570]">{footer}</span>}
        <span className="terminal-scan absolute inset-y-0 left-0 w-2/5" />
      </div>
    </figure>
  );
}
