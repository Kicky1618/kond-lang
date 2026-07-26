import type { ReactNode } from "react";
import { CodeBlock } from "./code-block";
import { ExternalIcon, LinkIcon } from "./icons";
import { slugify } from "@/app/lib/spec";

function inline(text: string): ReactNode[] {
  const pattern = /(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*|\[\^[^\]]+\]|\$[^$\n]+\$|\[[^\]]+\]\([^)]+\)|https?:\/\/[^\s)]+)/g;
  const nodes: ReactNode[] = [];
  let cursor = 0;
  let key = 0;
  for (const match of text.matchAll(pattern)) {
    const index = match.index ?? 0;
    if (index > cursor) nodes.push(text.slice(cursor, index));
    const token = match[0];
    if (token.startsWith("`")) nodes.push(<code key={key++}>{token.slice(1, -1)}</code>);
    else if (token.startsWith("**")) nodes.push(<strong key={key++}>{token.slice(2, -2)}</strong>);
    else if (token.startsWith("*")) nodes.push(<em key={key++}>{token.slice(1, -1)}</em>);
    else if (token.startsWith("[^")) {
      const id = token.slice(2, -1);
      nodes.push(<sup className="footnote-ref" key={key++}><a href={`#footnote-${id}`}>{id}</a></sup>);
    }
    else if (token.startsWith("$")) nodes.push(<span className="math-inline" key={key++}>{token.slice(1, -1)}</span>);
    else {
      const link = /^\[([^\]]+)\]\(([^)]+)\)$/.exec(token);
      const label = link?.[1] ?? token;
      const href = link?.[2] ?? token;
      const external = /^https?:\/\//.test(href);
      nodes.push(<a href={href} key={key++} rel={external ? "noreferrer noopener" : undefined} target={external ? "_blank" : undefined}>{label}{external && <ExternalIcon />}</a>);
    }
    cursor = index + token.length;
  }
  if (cursor < text.length) nodes.push(text.slice(cursor));
  return nodes;
}

function isBoundary(line: string): boolean {
  return !line.trim() || /^(#{1,4})\s|^```|^[-*]\s|^\d+\.\s|^>|^\|/.test(line);
}

export function Markdown({ source }: { source: string }) {
  const lines = source.split(/\r?\n/);
  const blocks: ReactNode[] = [];
  const ids = new Map<string, number>();
  let index = 0;

  while (index < lines.length) {
    const line = lines[index];
    if (!line.trim()) { index += 1; continue; }

    if (line.startsWith("$$")) {
      const math: string[] = [line.replace(/^\$\$/, "")];
      while (!math.at(-1)?.endsWith("$$") && ++index < lines.length) math.push(lines[index]);
      const value = math.join("\n").replace(/\$\$$/, "");
      blocks.push(<div aria-label="数式" className="math-block" key={`math-${index}`}>{value}</div>);
      index += 1;
      continue;
    }

    const footnote = /^\[\^([^\]]+)\]:\s*(.+)$/.exec(line);
    if (footnote) {
      blocks.push(<aside className="footnote" id={`footnote-${footnote[1]}`} key={`footnote-${footnote[1]}`}><sup>{footnote[1]}</sup><p>{inline(footnote[2])}</p></aside>);
      index += 1;
      continue;
    }

    const fence = /^```([^\s{]*)?(?:\s+(.+))?$/.exec(line);
    if (fence) {
      const code: string[] = [];
      const language = fence[1] || "text";
      const meta = fence[2] ?? "";
      index += 1;
      while (index < lines.length && !lines[index].startsWith("```")) code.push(lines[index++]);
      index += 1;
      const filename = /(?:title|filename)=["']?([^"'\s}]+)/.exec(meta)?.[1];
      const highlightRaw = /\{([\d,\-\s]+)\}/.exec(meta)?.[1] ?? "";
      const highlight = highlightRaw.split(",").flatMap((item) => {
        const [start, end] = item.trim().split("-").map(Number);
        if (!start) return [];
        return end ? Array.from({ length: end - start + 1 }, (_, offset) => start + offset) : [start];
      });
      blocks.push(<CodeBlock code={code.join("\n")} filename={filename} highlight={highlight} key={`code-${index}`} language={language} />);
      continue;
    }

    const heading = /^(#{1,4})\s+(.+)$/.exec(line);
    if (heading) {
      const depth = heading[1].length;
      const text = heading[2].replace(/\s+#+$/, "");
      const base = slugify(text.replace(/\[([^\]]+)\]\([^)]+\)/g, "$1").replace(/[*_`]/g, ""));
      const count = ids.get(base) ?? 0;
      ids.set(base, count + 1);
      const id = count ? `${base}-${count + 1}` : base;
      const content = <>{inline(text)}<a aria-label={`${text}へのリンク`} className="heading-anchor" href={`#${id}`}><LinkIcon /></a></>;
      if (depth === 1) blocks.push(<h1 id={id} key={id}>{content}</h1>);
      else if (depth === 2) blocks.push(<h2 id={id} key={id}>{content}</h2>);
      else if (depth === 3) blocks.push(<h3 id={id} key={id}>{content}</h3>);
      else blocks.push(<h4 id={id} key={id}>{content}</h4>);
      index += 1;
      continue;
    }

    if (line.startsWith(">")) {
      const quote: string[] = [];
      while (index < lines.length && lines[index].startsWith(">")) quote.push(lines[index++].replace(/^>\s?/, ""));
      const value = quote.join(" ");
      const kind = /^\[!(NOTE|WARNING|IMPORTANT)\]/i.exec(value)?.[1]?.toLowerCase();
      const clean = value.replace(/^\[![A-Z]+\]\s*/i, "");
      blocks.push(<aside className={`spec-notice ${kind ?? "note"}`} key={`quote-${index}`}><span>{kind === "warning" ? "警告" : kind === "important" ? "仕様上の注意" : "注記"}</span><p>{inline(clean)}</p></aside>);
      continue;
    }

    if (/^[-*]\s/.test(line)) {
      const items: string[] = [];
      while (index < lines.length && /^[-*]\s/.test(lines[index])) items.push(lines[index++].replace(/^[-*]\s+/, ""));
      blocks.push(<ul key={`ul-${index}`}>{items.map((item, itemIndex) => <li key={itemIndex}>{inline(item)}</li>)}</ul>);
      continue;
    }

    if (/^\d+\.\s/.test(line)) {
      const items: string[] = [];
      while (index < lines.length && /^\d+\.\s/.test(lines[index])) items.push(lines[index++].replace(/^\d+\.\s+/, ""));
      blocks.push(<ol key={`ol-${index}`}>{items.map((item, itemIndex) => <li key={itemIndex}>{inline(item)}</li>)}</ol>);
      continue;
    }

    if (line.startsWith("|") && lines[index + 1]?.match(/^\|?[\s:|-]+\|/)) {
      const rows: string[][] = [];
      while (index < lines.length && lines[index].startsWith("|")) rows.push(lines[index++].split("|").slice(1, -1).map((cell) => cell.trim()));
      const header = rows[0];
      const body = rows.slice(2);
      blocks.push(<div className="table-scroll" key={`table-${index}`}><table><thead><tr>{header.map((cell, cellIndex) => <th key={cellIndex}>{inline(cell)}</th>)}</tr></thead><tbody>{body.map((row, rowIndex) => <tr key={rowIndex}>{row.map((cell, cellIndex) => <td key={cellIndex}>{inline(cell)}</td>)}</tr>)}</tbody></table></div>);
      continue;
    }

    if (/^---+$/.test(line)) { blocks.push(<hr key={`hr-${index}`} />); index += 1; continue; }

    const paragraph: string[] = [line.trim()];
    index += 1;
    while (index < lines.length && !isBoundary(lines[index])) paragraph.push(lines[index++].trim());
    blocks.push(<p key={`p-${index}`}>{inline(paragraph.join(" "))}</p>);
  }

  return <div className="prose">{blocks}</div>;
}
