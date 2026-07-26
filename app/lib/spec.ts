import "server-only";

import fs from "node:fs";
import path from "node:path";

export type Heading = { depth: number; text: string; id: string };
export type DocPage = {
  slug: string;
  title: string;
  shortTitle: string;
  description: string;
  source: string;
  sourceLabel: string;
  language: "ja" | "en";
  order: number;
  headings: Heading[];
  body: string;
};

const ROOT = process.cwd();
const SPEC = path.join(ROOT, "spec");

export function slugify(value: string): string {
  const clean = value
    .toLowerCase()
    .replace(/`/g, "")
    .replace(/&/g, " and ")
    .replace(/[^\p{L}\p{N}]+/gu, "-")
    .replace(/^-+|-+$/g, "");
  return clean || "section";
}

export function extractHeadings(markdown: string): Heading[] {
  const used = new Map<string, number>();
  return markdown.split(/\r?\n/).flatMap((line) => {
    const match = /^(#{1,4})\s+(.+?)\s*$/.exec(line);
    if (!match) return [];
    const text = match[2].replace(/\[([^\]]+)\]\([^)]+\)/g, "$1").replace(/[*_]/g, "");
    const base = slugify(text);
    const count = used.get(base) ?? 0;
    used.set(base, count + 1);
    return [{ depth: match[1].length, text, id: count === 0 ? base : `${base}-${count + 1}` }];
  });
}

function firstParagraph(markdown: string): string {
  const lines = markdown.split(/\r?\n/);
  let paragraph = "";
  let fenced = false;
  for (const line of lines.slice(1)) {
    if (line.startsWith("```")) fenced = !fenced;
    if (fenced || !line.trim() || /^#|^[-*]\s|^>|^\|/.test(line)) {
      if (paragraph) break;
      continue;
    }
    paragraph += `${paragraph ? " " : ""}${line.trim()}`;
  }
  return paragraph.replace(/[*_`]/g, "").slice(0, 180);
}

const chapterFiles = fs
  .readdirSync(path.join(SPEC, "docs"))
  .filter((name) => name.endsWith(".md"))
  .sort((a, b) => a.localeCompare(b, "en", { numeric: true }));

function loadFile(relative: string, slug: string, order: number, language: "ja" | "en" = "en"): DocPage {
  const absolute = path.join(ROOT, relative);
  const body = fs.readFileSync(absolute, "utf8");
  const headings = extractHeadings(body);
  const title = headings[0]?.text ?? path.basename(relative, ".md");
  return {
    slug,
    title,
    shortTitle: title.replace(/^\d+[A-Z]?\.\s*/, ""),
    description: firstParagraph(body),
    source: relative,
    sourceLabel: relative.replace(/^spec\//, ""),
    language,
    order,
    headings,
    body
  };
}

export const chapterDocs: DocPage[] = chapterFiles.map((filename, index) =>
  loadFile(`spec/docs/${filename}`, filename.replace(/^\d+[a-z]?-/i, "").replace(/\.md$/, ""), index + 10)
);

export const specialDocs: DocPage[] = [
  loadFile("spec/SPEC.ja.md", "specification-ja", 1, "ja"),
  loadFile("spec/SPEC.md", "specification-en", 2),
  loadFile("spec/README.md", "project-overview", 3),
  loadFile("spec/CHANGELOG.md", "changelog", 4),
  loadFile("spec/REFERENCES.md", "references", 5)
];

export const docs = [...specialDocs, ...chapterDocs];

export function getDoc(slug: string): DocPage | undefined {
  return docs.find((doc) => doc.slug === slug);
}

export function getDraft(): string {
  const manifest = JSON.parse(fs.readFileSync(path.join(SPEC, "manifest.json"), "utf8")) as { status?: string };
  return manifest.status ?? "Experimental draft";
}

export type SearchEntry = {
  title: string;
  heading: string;
  excerpt: string;
  url: string;
  text: string;
};

function plain(markdown: string): string {
  return markdown
    .replace(/```[\s\S]*?```/g, (code) => code.replace(/```[^\n]*\n?|```/g, " "))
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/[#>*_`|]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

export function buildSearchIndex(): SearchEntry[] {
  return docs.flatMap((doc) => {
    const chunks = doc.body.split(/(?=^#{2,3}\s)/m);
    return chunks.map((chunk, index) => {
      const heading = /^#{2,3}\s+(.+)$/m.exec(chunk)?.[1]?.replace(/[*_`]/g, "") ?? doc.title;
      const headingInfo = doc.headings.find((item) => item.text === heading);
      const text = plain(chunk);
      return {
        title: doc.shortTitle,
        heading,
        excerpt: text.slice(0, 210),
        url: `/docs/${doc.slug}/${index === 0 || !headingInfo ? "" : `#${headingInfo.id}`}`,
        text: `${doc.title} ${heading} ${text}`.toLowerCase()
      };
    });
  });
}

export function getGrammar(): string {
  return fs.readFileSync(path.join(SPEC, "syntax.ebnf"), "utf8");
}
