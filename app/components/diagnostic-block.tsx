import { CodeBlock } from "./code-block";

export type Diagnostic = {
  code: string;
  title: string;
  category: string;
  source?: string;
  output: string;
  cause: string;
  fix: string;
};

export function DiagnosticBlock({ diagnostic }: { diagnostic: Diagnostic }) {
  return <article className="diagnostic-block" id={diagnostic.code.toLowerCase()}>
    <header><div><span>{diagnostic.category}</span><h2><code>{diagnostic.code}</code>{diagnostic.title}</h2></div><a href={`#${diagnostic.code.toLowerCase()}`}>#</a></header>
    {diagnostic.source && <CodeBlock code={diagnostic.source} filename="src/main.kd" language="kond" />}
    <CodeBlock code={diagnostic.output} filename="kond check" language="console" />
    <dl><div><dt>原因</dt><dd>{diagnostic.cause}</dd></div><div><dt>修正方針</dt><dd>{diagnostic.fix}</dd></div></dl>
  </article>;
}
