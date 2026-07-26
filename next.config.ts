import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  basePath: "/kond-lang",
  output: "export",
  trailingSlash: true,
  images: { unoptimized: true },
  reactStrictMode: true
};

export default nextConfig;
