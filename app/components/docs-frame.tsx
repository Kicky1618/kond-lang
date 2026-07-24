import DocsSidebar from "./docs-sidebar";
import DocsToc from "./docs-toc";
import { getDoc } from "../lib/docs";

export default function DocsFrame({ children, currentSlug }: { children: React.ReactNode; currentSlug?: string }) {
  const currentDoc = currentSlug ? getDoc(currentSlug) : undefined;

  return (
    <div className={`mx-auto grid max-w-[1320px] gap-10 px-5 py-8 sm:px-8 md:py-12 lg:grid-cols-[230px_minmax(0,1fr)] lg:gap-14 lg:px-10 lg:py-14 ${currentDoc ? "xl:grid-cols-[220px_minmax(0,760px)_180px] xl:gap-12" : ""}`}>
      <DocsSidebar currentSlug={currentSlug} />
      {children}
      {currentDoc && <DocsToc sections={currentDoc.sections} />}
    </div>
  );
}
