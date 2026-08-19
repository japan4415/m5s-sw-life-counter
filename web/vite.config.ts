import { dirname, resolve } from "path";
import { fileURLToPath } from "url";
import { defineConfig } from "vite";

const __dirname = dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  build: {
    // Vite 8 のデフォルト CSS minifier は -webkit- 付き宣言があると標準の
    // backdrop-filter 宣言を除去してしまう。esbuild は両方保持するため明示指定する。
    cssMinify: "esbuild",
    outDir: "dist",
    rollupOptions: {
      input: {
        main: resolve(__dirname, "index.html"),
        install: resolve(__dirname, "install/index.html"),
        guide: resolve(__dirname, "guide/index.html"),
        features: resolve(__dirname, "features/index.html"),
        notFound: resolve(__dirname, "404.html"),
      },
    },
  },
});
