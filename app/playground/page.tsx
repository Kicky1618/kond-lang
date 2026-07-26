import type { Metadata } from "next";
import fs from "node:fs";
import path from "node:path";
import { Playground, type Example } from "@/app/components/playground";

export const metadata: Metadata = { title: "Annotated examples", description: "既存の Kond サンプルと条件・証明状態を左右に並べたプレイグラウンド風ビュー。", alternates: { canonical: "https://kicky1618.github.io/kond-lang/playground/" } };

function read(file: string): string { return fs.readFileSync(path.join(process.cwd(), file), "utf8").trim(); }

const examples: Example[] = [
  { title: "Condition 分岐", filename: "examples/condition_value.kd", source: read("examples/condition_value.kd"), state: ["known:", "  x = 3", "  positive: Condition<x > 0>", "", "after check:", "  Proof<x > 0> available", "", "branch:", "  holds proof imported"], note: "条件値を check / prove し、if の成立側へ evidence を取り込む例です。" },
  { title: "条件保存変数", filename: "examples/order_processing.kd", source: read("examples/order_processing.kd"), state: ["target invariant:", "  Order(order)", "", "update target:", "  order.items", "", "required after update:", "  revalidate Order(order)", "", "result:", "  commit only after check"], note: "update block の変更候補は、外側の Order 条件を再検証してから確定します。" },
  { title: "Ownership move", filename: "examples/ownership.kd", source: read("examples/ownership.kd"), state: ["before move:", "  Own(xs)", "", "operation:", "  let ys = move xs", "", "after move:", "  Own(ys)", "  Moved(xs)", "", "result:", "  xs can no longer be consumed"], note: "move は所有権という linear fact を新しい binding へ移します。" },
  { title: "Shared / unique borrow", filename: "examples/unique_borrow.kd", source: read("examples/unique_borrow.kd"), state: ["entry:", "  Own(value)", "", "inside scope:", "  BorrowUnique(value)", "  owner temporarily unavailable", "", "scope exit:", "  borrow ends", "  Own(value₁) restored"], note: "unique borrow は lexical scope に結びつき、終了時に更新された value version の所有権を復元します。" },
  { title: "Web 入力検証", filename: "spec/examples/server.kd", source: read("spec/examples/server.kd"), state: ["boundary:", "  Any(body)", "  Untrusted(body)", "", "require:", "  UserInput(body)", "", "sink contracts:", "  parameterized SQL", "  context-safe HTML"], note: "route 境界で得た入力を検証し、関数契約と安全な structured literal へ evidence を渡します。" },
  { title: "SQL 安全補間", filename: "examples/order_processing.kd", source: read("examples/order_processing.kd"), state: ["input flow:", "  Untrusted(order.customer)", "", "sql template:", "  interpolation parameterized", "", "result:", "  SQL sink contract satisfied", "", "contrast:", "  string concatenation is unsafe"], note: "sql tagged template は補間を parameterize します。単なる文字列連結とは異なります。" },
  { title: "Proof 最適化", filename: "examples/v02_math_opt.kd", source: read("examples/v02_math_opt.kd"), state: ["preconditions:", "  Int(x)", "  x % 2 == 0", "", "candidate:", "  (x / 2) * 2  →  x", "", "proof class:", "  ExactEq", "", "result:", "  rewrite may be applied"], note: "関数の requires が成立するときだけ、exact catalogue の書き換えを適用できます。" }
];

export default function PlaygroundPage() {
  return <main className="standalone wide" id="main"><header className="page-hero compact"><span>ANNOTATED EXAMPLES · READ-ONLY</span><h1>Code on the left.<br /><em>Evidence on the right.</em></h1><p>実行環境ではありません。左はリポジトリ内の実例、右は仕様理解のための非規範的な状態トレースです。</p></header><Playground examples={examples} /></main>;
}
