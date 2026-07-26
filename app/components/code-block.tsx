import { highlightLine } from "@/app/lib/highlight";
import { CopyButton } from "./client-controls";

export function CodeBlock({ code, language = "text", filename, highlight = [], lineIds = {} }: { code: string; language?: string; filename?: string; highlight?: number[]; lineIds?: Record<number, string> }) {
  const clean = code.replace(/\n$/, "");
  const lines = clean.split("\n");
  return <figure className="code-block">
    <figcaption><span>{filename ?? (language === "kond" || language === "kd" ? "example.kd" : language)}</span><CopyButton value={clean} /></figcaption>
    <pre aria-label={`${language} code`} className="code-scroll"><code>{lines.map((line, index) =>
      <span className={`code-line${highlight.includes(index + 1) ? " is-highlighted" : ""}`} data-line={index + 1} id={lineIds[index + 1]} key={index}>{highlightLine(line, language, index)}</span>
    )}</code></pre>
  </figure>;
}
