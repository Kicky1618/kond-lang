import type { ReactNode } from "react";

const keywords = new Set([
  "let", "where", "condition", "fn", "requires", "ensures", "check", "require",
  "prove", "assume", "unsafe", "if", "else", "match", "when", "while",
  "invariant", "update", "move", "route", "flow", "return", "and", "or", "not",
  "is", "has", "always", "never", "borrow", "shared", "unique"
]);
const builtins = new Set([
  "Condition", "LinearCondition", "Invariant", "Any", "Proof", "Own",
  "BorrowShared", "BorrowUnique"
]);

const tokenPattern =
  /(\/\/.*|#(?!\s).*|\(\*.*?\*\))|("(?:[^"\\]|\\.)*")|(\b\d+(?:\.\d+)?\b)|(\b[A-Za-z_][A-Za-z0-9_]*\b)/g;

export function highlightLine(line: string, lang: string, lineNumber: number): ReactNode[] {
  if (!["kond", "kd", "ebnf", "console", "text", "shell", "bash"].includes(lang)) return [line || " "];
  const nodes: ReactNode[] = [];
  let cursor = 0;
  let tokenIndex = 0;
  for (const match of line.matchAll(tokenPattern)) {
    const index = match.index ?? 0;
    if (index > cursor) nodes.push(line.slice(cursor, index));
    const [token, comment, string, number, word] = match;
    let className = "";
    if (comment) className = "tok-comment";
    else if (string) className = "tok-string";
    else if (number) className = "tok-number";
    else if (word && keywords.has(word)) className = "tok-keyword";
    else if (word && builtins.has(word)) className = "tok-type";
    else if (word && /^[A-Z]/.test(word)) className = "tok-constant";
    if (className) {
      nodes.push(<span className={className} key={`${lineNumber}-${tokenIndex++}`}>{token}</span>);
    } else {
      nodes.push(token);
    }
    cursor = index + token.length;
  }
  if (cursor < line.length) nodes.push(line.slice(cursor));
  return nodes.length ? nodes : [" "];
}
