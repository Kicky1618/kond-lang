import {
  defineConfig,
  presetAttributify,
  presetTypography,
  presetUno,
} from "unocss";

export default defineConfig({
  presets: [presetUno(), presetAttributify(), presetTypography()],
  theme: {
    colors: {
      ink: "#0a0d12",
      paper: "#f5f7f2",
      lime: "#c8ff70",
      cyan: "#80e7e9",
      smoke: "#9aa5a3",
    },
    fontFamily: {
      sans: "var(--font-sans)",
      mono: "var(--font-mono)",
    },
  },
  shortcuts: {
    "eyebrow": "font-mono text-[10px] uppercase tracking-[0.18em] text-lime",
    "glass-panel": "border border-white/10 bg-white/[0.035] backdrop-blur-sm",
    "button-primary": "inline-flex items-center justify-center gap-2 rounded-full bg-lime px-5 py-3 text-sm font-700 text-ink transition-transform duration-200 hover:translate-y-[-2px]",
    "button-secondary": "inline-flex items-center justify-center gap-2 rounded-full border border-white/16 px-5 py-3 text-sm font-600 text-white transition-colors duration-200 hover:border-lime/60 hover:text-lime",
  },
});
