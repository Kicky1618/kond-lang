import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const files = [
  ...fs.readdirSync(path.join(root, "spec")).filter((file) => file.endsWith(".md")).map((file) => path.join(root, "spec", file)),
  ...fs.readdirSync(path.join(root, "spec/docs")).filter((file) => file.endsWith(".md")).map((file) => path.join(root, "spec/docs", file))
];
const failures: string[] = [];
for (const file of files) {
  const source = fs.readFileSync(file, "utf8");
  for (const match of source.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)) {
    const target = match[1];
    if (/^(https?:|#|mailto:)/.test(target)) continue;
    const clean = target.split("#")[0];
    if (clean && !fs.existsSync(path.resolve(path.dirname(file), clean))) failures.push(`${path.relative(root, file)} -> ${target}`);
  }
}
if (failures.length) {
  console.error(`Broken Markdown links:\n${failures.join("\n")}`);
  process.exit(1);
}
console.log(`Checked ${files.length} Markdown files: all local link targets exist.`);
