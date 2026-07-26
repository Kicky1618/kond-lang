import type { MetadataRoute } from "next";
import { docs } from "./lib/spec";

export const dynamic = "force-static";

export default function sitemap(): MetadataRoute.Sitemap {
  const base = "https://kicky1618.github.io/kond-lang";
  const routes = ["", "/overview", "/playground", "/diagnostics", "/grammar", "/related-work"];
  return [...routes.map((route) => ({ url: `${base}${route}/`, changeFrequency: "weekly" as const, priority: route ? 0.8 : 1 })), ...docs.map((doc) => ({ url: `${base}/docs/${doc.slug}/`, changeFrequency: "weekly" as const, priority: 0.7 }))];
}
