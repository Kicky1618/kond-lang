"use client";

import Link from "next/link";
import { useEffect, useMemo, useRef, useState } from "react";
import { docGroups, docs } from "../lib/docs";
import { SearchIcon } from "./icons";
import { Pill } from "./ui";

export default function DocsSidebar({ currentSlug }: { currentSlug?: string }) {
  const [query, setQuery] = useState("");
  const [isOpen, setIsOpen] = useState(false);
  const searchRef = useRef<HTMLInputElement>(null);
  const normalizedQuery = query.trim().toLowerCase();
  const currentDoc = docs.find((doc) => doc.slug === currentSlug);
  const filteredDocs = useMemo(
    () => docs.filter((doc) => `${doc.title} ${doc.category} ${doc.description}`.toLowerCase().includes(normalizedQuery)),
    [normalizedQuery],
  );

  useEffect(() => {
    function onShortcut(event: KeyboardEvent) {
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        setIsOpen(true);
        requestAnimationFrame(() => searchRef.current?.focus());
      }
    }
    window.addEventListener("keydown", onShortcut);
    return () => window.removeEventListener("keydown", onShortcut);
  }, []);

  return (
    <aside className="doc-sidebar self-start lg:sticky lg:top-6 lg:max-h-[calc(100vh-3rem)] lg:overflow-y-auto lg:pr-2">
      <button
        aria-controls="docs-navigation"
        aria-expanded={isOpen}
        className="flex w-full items-center justify-between rounded-[12px] border border-white/10 bg-[#11171c] px-4 py-3 text-left shadow-lg lg:hidden"
        onClick={() => setIsOpen((open) => !open)}
        type="button"
      >
        <span>
          <span className="block font-mono text-[8px] uppercase tracking-[0.16em] text-[#66736e]">Documentation</span>
          <span className="mt-1 block text-[13px] font-600 text-white">{currentDoc ? `${currentDoc.number} · ${currentDoc.shortTitle}` : "Overview"}</span>
        </span>
        <span className={`flex h-7 w-7 items-center justify-center rounded-full border border-white/10 font-mono text-lime transition-transform ${isOpen ? "rotate-180" : ""}`}>⌄</span>
      </button>

      <div className={`${isOpen ? "block" : "hidden"} mt-3 rounded-[16px] border border-white/10 bg-[#0e1418] p-4 lg:mt-0 lg:block lg:bg-white/[0.025]`}>
        <div className="flex items-center justify-between">
          <p className="font-mono text-[10px] uppercase tracking-[0.16em] text-[#7d8984]">Explore docs</p>
          <Pill>v0.2</Pill>
        </div>
        <label className="mt-4 flex items-center gap-2 rounded-[9px] border border-white/10 bg-ink/35 px-3 py-2.5 text-[#73807b] focus-within:border-lime/45">
          <SearchIcon className="h-3.5 w-3.5 shrink-0" />
          <input aria-label="ドキュメントを検索" className="min-w-0 flex-1 bg-transparent font-mono text-[11px] text-white outline-none placeholder:text-[#66736e]" onChange={(event) => setQuery(event.target.value)} placeholder="Search docs" ref={searchRef} value={query} />
          <span className="hidden rounded border border-white/10 px-1.5 py-0.5 font-mono text-[9px] text-[#687570] sm:inline">⌘K</span>
        </label>
      </div>

      <div className={`${isOpen ? "block" : "hidden"} lg:block`} id="docs-navigation">
        {normalizedQuery ? (
          <div className="mt-5 space-y-1">
            {filteredDocs.map((doc) => (
              <Link className={`block rounded-[9px] px-3 py-2.5 text-[12px] transition-colors ${currentSlug === doc.slug ? "bg-lime/10 text-lime" : "text-[#aeb8b4] hover:bg-white/5 hover:text-white"}`} href={`/docs/${doc.slug}`} key={doc.slug}>
                <span className="mr-2 font-mono text-[9px] text-[#66736e]">{doc.number}</span>{doc.title}
              </Link>
            ))}
            {filteredDocs.length === 0 && <p className="px-3 py-3 text-[12px] leading-6 text-[#71807a]">No matching docs yet.</p>}
          </div>
        ) : (
          <nav aria-label="ドキュメント目次" className="mt-6 space-y-5 px-1">
            <Link className={`flex items-center gap-2 px-3 py-1.5 font-mono text-[10px] uppercase tracking-[0.13em] transition-colors ${!currentSlug ? "text-lime" : "text-[#899591] hover:text-white"}`} href="/docs">
              <span className="h-1.5 w-1.5 rounded-full bg-current" />Overview
            </Link>
            {docGroups.map((group) => (
              <div key={group.label}>
                <p className="px-3 font-mono text-[9px] uppercase tracking-[0.18em] text-[#586560]">{group.label}</p>
                <div className="mt-2 space-y-0.5">
                  {group.slugs.map((slug) => {
                    const doc = docs.find((item) => item.slug === slug);
                    if (!doc) return null;
                    return (
                      <Link className={`flex items-center gap-2 rounded-[8px] px-3 py-2 text-[12px] transition-colors ${currentSlug === doc.slug ? "bg-lime/10 text-lime" : "text-[#9ca8a3] hover:bg-white/5 hover:text-white"}`} href={`/docs/${doc.slug}`} key={doc.slug}>
                        <span className="font-mono text-[9px] text-[#65726d]">{doc.number}</span>{doc.shortTitle}
                      </Link>
                    );
                  })}
                </div>
              </div>
            ))}
          </nav>
        )}

        <div className="mt-8 border-t border-white/10 px-3 pt-5">
          <p className="font-mono text-[9px] uppercase tracking-[0.16em] text-[#596660]">Specification status</p>
          <div className="mt-3 flex items-center gap-2 text-[11px] text-[#9eaaa5]"><span className="h-1.5 w-1.5 rounded-full bg-lime shadow-[0_0_8px_#c8ff70]" />Draft 0.2 / experimental</div>
        </div>
      </div>
    </aside>
  );
}
