import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  basePath: "/kond-lang",
  reactStrictMode: true,
  output: "export",
  trailingSlash: true,
};

export default nextConfig;
