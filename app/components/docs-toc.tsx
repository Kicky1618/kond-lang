"use client";

import { useEffect, useState } from "react";
import type { DocSection } from "../lib/docs";
import { sectionId } from "../lib/docs";

export default function DocsToc({ sections }: { sections: DocSection[] }) {
  const [activeId, setActiveId] = useState(sectionId(sections[0]?.heading ?? ""));

  useEffect(() => {
    const headings = sections
      .map((section) => document.getElementById(sectionId(section.heading)))
      .filter((heading): heading is HTMLElement => Boolean(heading));

    const observer = new IntersectionObserver(
      (entries) => {
        const visible = entries
          .filter((entry) => entry.isIntersecting)
          .sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top);
        if (visible[0]) setActiveId(visible[0].target.id);
      },
      { rootMargin: "-18% 0px -68% 0px" },
    );

    headings.forEach((heading) => observer.observe(heading));
    return () => observer.disconnect();
  }, [sections]);

  return (
    <aside className="hidden self-start xl:sticky xl:top-8 xl:block">
      <p className="font-mono text-[9px] uppercase tracking-[0.18em] text-[#64716c]">In this chapter</p>
      <nav aria-label="この章の目次" className="mt-4 border-l border-white/10">
        {sections.map((section, index) => {
          const id = sectionId(section.heading);
          const isActive = id === activeId;
          return (
            <a
              className={`-ml-px flex gap-3 border-l px-4 py-2.5 text-[11px] leading-[1.55] transition-colors ${
                isActive ? "border-lime text-white" : "border-transparent text-[#78857f] hover:text-[#c5cfca]"
              }`}
              href={`#${id}`}
              key={section.heading}
            >
              <span className={`font-mono text-[9px] ${isActive ? "text-lime" : "text-[#56635e]"}`}>
                {String(index + 1).padStart(2, "0")}
              </span>
              {section.heading}
            </a>
          );
        })}
      </nav>
      <a className="mt-6 inline-flex items-center gap-2 font-mono text-[9px] uppercase tracking-[0.12em] text-[#697670] transition-colors hover:text-lime" href="#top">
        Back to top <span aria-hidden="true">↑</span>
      </a>
    </aside>
  );
}
