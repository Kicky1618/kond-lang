import type { MetadataRoute } from "next";
export const dynamic = "force-static";
export default function robots(): MetadataRoute.Robots { return { rules: { userAgent: "*", allow: "/kond-lang/" }, sitemap: "https://kicky1618.github.io/kond-lang/sitemap.xml" }; }
