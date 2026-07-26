"use client";

import { useState } from "react";
import { CodeBlock } from "./code-block";

export type Example = { title: string; source: string; filename: string; state: string[]; note: string };

export function Playground({ examples }: { examples: Example[] }) {
  const [active, setActive] = useState(0);
  const example = examples[active];
  return <div className="playground">
    <div aria-label="サンプル選択" className="example-tabs" role="tablist">{examples.map((item, index) => <button aria-selected={index === active} key={item.title} onClick={() => setActive(index)} role="tab" type="button">{item.title}</button>)}</div>
    <div className="playground-grid">
      <div><div className="pane-label"><span>01</span>SOURCE · {example.filename}</div><CodeBlock code={example.source} filename={example.filename} language="kond" /></div>
      <div className="proof-state"><div className="pane-label"><span>02</span>NON-NORMATIVE TRACE</div><div className="state-lines">{example.state.map((line, index) => <div className={line.endsWith(":") ? "label" : ""} key={index}>{line || "\u00a0"}</div>)}</div><p><strong>解説</strong>{example.note}</p></div>
    </div>
  </div>;
}
