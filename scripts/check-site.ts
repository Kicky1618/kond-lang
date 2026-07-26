import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const out = path.join(root, "out");
const required = [
  "index.html",
  "overview/index.html",
  "playground/index.html",
  "diagnostics/index.html",
  "grammar/index.html",
  "related-work/index.html",
  "docs/specification-ja/index.html",
  "docs/specification-en/index.html",
  "sitemap.xml",
  "robots.txt"
];
const failures = required.filter((file) => !fs.existsSync(path.join(out, file))).map((file) => `missing output: ${file}`);

const pages: string[] = [];
function collect(directory: string) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const target = path.join(directory, entry.name);
    if (entry.isDirectory()) collect(target);
    else if (entry.name === "index.html") pages.push(target);
  }
}
collect(out);

for (const file of pages) {
  const html = fs.readFileSync(file, "utf8");
  const documentHtml = html.split("<script>self.__next_f")[0];
  const ids = [...documentHtml.matchAll(/\sid="([^"]+)"/g)].map((match) => match[1]);
  const duplicates = ids.filter((id, index) => ids.indexOf(id) !== index);
  if (duplicates.length) failures.push(`${path.relative(out, file)}: duplicate anchors ${[...new Set(duplicates)].join(", ")}`);
  for (const match of documentHtml.matchAll(/href="(\/kond-lang\/[^"#?]*)(?:#[^"]*)?"/g)) {
    const href = match[1].replace(/^\/kond-lang\//, "");
    if (!href || href.startsWith("_next/")) continue;
    const target = path.join(out, href, href.endsWith(".xml") || href.endsWith(".txt") ? "" : "index.html");
    if (!fs.existsSync(target)) failures.push(`${path.relative(out, file)} -> ${match[1]}`);
  }
}

if (failures.length) {
  console.error(`Site validation failed:\n${failures.join("\n")}`);
  process.exit(1);
}
console.log(`Validated ${pages.length} generated pages, required routes, internal links, and unique anchors.`);
