"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import Link from "next/link";
import type { SearchEntry } from "@/app/lib/spec";
import { CheckIcon, CloseIcon, CopyIcon, MenuIcon, MoonIcon, SearchIcon, SunIcon } from "./icons";

export function ThemeSwitcher() {
  const [dark, setDark] = useState(true);
  useEffect(() => {
    const saved = localStorage.getItem("kond-theme");
    const nextDark = saved ? saved === "dark" : window.matchMedia("(prefers-color-scheme: dark)").matches;
    document.documentElement.dataset.theme = nextDark ? "dark" : "light";
    setDark(nextDark);
  }, []);
  function toggle() {
    const next = !dark;
    setDark(next);
    document.documentElement.dataset.theme = next ? "dark" : "light";
    localStorage.setItem("kond-theme", next ? "dark" : "light");
  }
  return <button aria-label={dark ? "ライトテーマに切り替え" : "ダークテーマに切り替え"} className="icon-button" onClick={toggle} type="button">{dark ? <SunIcon /> : <MoonIcon />}</button>;
}

export function CopyButton({ value, label = "コードをコピー" }: { value: string; label?: string }) {
  const [copied, setCopied] = useState(false);
  async function copy() {
    await navigator.clipboard.writeText(value);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1500);
  }
  return <button aria-label={label} className="copy-button" onClick={copy} type="button">{copied ? <><CheckIcon /> コピー済み</> : <><CopyIcon /> コピー</>}</button>;
}

export function MobileNavigation({ children }: { children: React.ReactNode }) {
  const [open, setOpen] = useState(false);
  return <>
    <button aria-expanded={open} aria-label="ドキュメントメニュー" className="mobile-nav-button" onClick={() => setOpen(true)} type="button"><MenuIcon /> 目次</button>
    {open && <div className="drawer-backdrop" onClick={() => setOpen(false)}>
      <aside aria-label="モバイルドキュメントナビゲーション" className="drawer" onClick={(event) => event.stopPropagation()}>
        <button aria-label="メニューを閉じる" className="drawer-close" onClick={() => setOpen(false)} type="button"><CloseIcon /></button>
        <div onClick={() => setOpen(false)}>{children}</div>
      </aside>
    </div>}
  </>;
}

export function SearchDialog({ entries }: { entries: SearchEntry[] }) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState("");
  const input = useRef<HTMLInputElement>(null);
  useEffect(() => {
    function keydown(event: KeyboardEvent) {
      const tag = (event.target as HTMLElement | null)?.tagName;
      if ((event.key === "k" && (event.metaKey || event.ctrlKey)) || (event.key === "/" && tag !== "INPUT" && tag !== "TEXTAREA")) {
        event.preventDefault();
        setOpen(true);
      }
      if (event.key === "Escape") setOpen(false);
    }
    window.addEventListener("keydown", keydown);
    return () => window.removeEventListener("keydown", keydown);
  }, []);
  useEffect(() => { if (open) window.setTimeout(() => input.current?.focus(), 0); }, [open]);
  const results = useMemo(() => {
    const terms = query.toLowerCase().trim().split(/\s+/).filter(Boolean);
    if (!terms.length) return entries.slice(0, 8);
    return entries.filter((entry) => terms.every((term) => entry.text.includes(term))).slice(0, 12);
  }, [entries, query]);
  return <>
    <button aria-label="検索を開く" className="search-trigger" onClick={() => setOpen(true)} type="button"><SearchIcon /><span>仕様を検索</span><kbd>⌘ K</kbd></button>
    {open && <div aria-label="全文検索" aria-modal="true" className="search-backdrop" role="dialog" onMouseDown={() => setOpen(false)}>
      <div className="search-panel" onMouseDown={(event) => event.stopPropagation()}>
        <div className="search-input-row"><SearchIcon /><input aria-label="検索語" onChange={(event) => setQuery(event.target.value)} placeholder="Condition, ownership, E1204…" ref={input} value={query} /><button aria-label="検索を閉じる" onClick={() => setOpen(false)} type="button"><CloseIcon /></button></div>
        <p className="search-count">{query ? `${results.length} 件の結果` : "仕様書、コード例、診断コードを検索"}</p>
        <div className="search-results">
          {results.map((entry) => <Link href={entry.url} key={`${entry.url}-${entry.heading}`} onClick={() => setOpen(false)}>
            <span>{entry.title}</span><strong>{entry.heading}</strong><p>{entry.excerpt}</p>
          </Link>)}
          {!results.length && <div className="search-empty">一致する項目がありません。別の語を試してください。</div>}
        </div>
      </div>
    </div>}
  </>;
}
