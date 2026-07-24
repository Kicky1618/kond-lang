import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDirectory = path.dirname(fileURLToPath(import.meta.url));

export default {
  plugins: {
    [path.join(rootDirectory, "postcss-unocss.cjs")]: {},
  },
};
