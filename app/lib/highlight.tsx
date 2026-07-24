import type { ReactNode } from "react";

export type CodeLang = "kond" | "console";

const KEYWORDS = new Set([
  "fn", "let", "where", "self", "is", "has", "check", "prove", "require",
  "requires", "ensures", "result", "if", "else", "while", "for", "in",
  "invariant", "condition", "return", "and", "or", "not", "move", "borrow",
  "update", "unsafe", "use", "export", "match", "mut", "untrusted",
  "declassify", "endorse",
]);

const TOKEN_RE =
  /(\/\/.*)|([A-Za-z_][A-Za-z0-9_]*)?("(?:[^"\\]|\\.)*")|(\d[\d_]*(?:\.\d+)?)|([A-Za-z_][A-Za-z0-9_]*)/g;

function highlightKondLine(line: string, lineIndex: number): ReactNode[] {
  const nodes: ReactNode[] = [];
  let last = 0;
  let seq = 0;
  const key = () => `${lineIndex}:${seq++}`;

  for (const match of line.matchAll(TOKEN_RE)) {
    const [full, comment, stringPrefix, stringLiteral, number, identifier] = match;
    const index = match.index ?? 0;
    if (index > last) nodes.push(line.slice(last, index));

    if (comment !== undefined) {
      nodes.push(
        <span className="syntax-comment" key={key()}>
          {comment}
        </span>,
      );
    } else if (stringLiteral !== undefined) {
      if (stringPrefix) {
        nodes.push(
          <span className="syntax-keyword" key={key()}>
            {stringPrefix}
          </span>,
        );
      }
      nodes.push(
        <span className="syntax-string" key={key()}>
          {stringLiteral}
        </span>,
      );
    } else if (number !== undefined) {
      nodes.push(
        <span className="syntax-number" key={key()}>
          {number}
        </span>,
      );
    } else if (identifier !== undefined) {
      if (KEYWORDS.has(identifier)) {
        nodes.push(
          <span className="syntax-keyword" key={key()}>
            {identifier}
          </span>,
        );
      } else if (/^[A-Z]/.test(identifier)) {
        nodes.push(
          <span className="syntax-type" key={key()}>
            {identifier}
          </span>,
        );
      } else if (line[index + full.length] === "(") {
        nodes.push(
          <span className="syntax-fn" key={key()}>
            {identifier}
          </span>,
        );
      } else {
        nodes.push(identifier);
      }
    }
    last = index + full.length;
  }

  if (last < line.length) nodes.push(line.slice(last));
  return nodes.length > 0 ? nodes : [" "];
}

function highlightConsoleLine(line: string, lineIndex: number): ReactNode[] {
  const severity = line.match(/^(error|warning)(\[[^\]]+\])?(:.*)?$/);
  if (severity) {
    return [
      <span className="syntax-error" key={`${lineIndex}:s`}>
        {severity[1]}
        {severity[2] ?? ""}
      </span>,
      severity[3] ?? "",
    ];
  }
  const label = line.match(/^(\s*)([a-z_]+)(:)(.*)$/);
  if (label) {
    return [
      label[1],
      <span className="syntax-label" key={`${lineIndex}:l`}>
        {label[2]}
        {label[3]}
      </span>,
      label[4],
    ];
  }
  return [line || " "];
}

export function highlightLine(line: string, lang: CodeLang, lineIndex: number): ReactNode[] {
  return lang === "console" ? highlightConsoleLine(line, lineIndex) : highlightKondLine(line, lineIndex);
}
