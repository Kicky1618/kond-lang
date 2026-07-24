"use client";

import { useEffect, useRef, useState } from "react";
import { CheckIcon, CopyIcon } from "./icons";

export default function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false);
  const timer = useRef<number | null>(null);

  useEffect(() => {
    return () => {
      if (timer.current !== null) window.clearTimeout(timer.current);
    };
  }, []);

  async function copy() {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.style.position = "fixed";
      textarea.style.opacity = "0";
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand("copy");
      textarea.remove();
    }
    setCopied(true);
    if (timer.current !== null) window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => setCopied(false), 1600);
  }

  return (
    <button
      aria-label="コードをコピー"
      className={`group flex h-7 items-center gap-1.5 rounded-[7px] border px-2 font-mono text-[8px] uppercase tracking-[0.12em] transition-all ${
        copied
          ? "border-lime/25 bg-lime/[0.07] text-lime"
          : "border-white/[0.08] text-[#77847e] hover:border-white/20 hover:bg-white/[0.04] hover:text-white"
      }`}
      onClick={copy}
      type="button"
    >
      {copied ? (
        <>
          <CheckIcon className="h-3 w-3" />
          <span className="hidden sm:inline">Copied</span>
        </>
      ) : (
        <>
          <CopyIcon className="h-3 w-3" />
          <span className="hidden sm:inline">Copy</span>
        </>
      )}
    </button>
  );
}
