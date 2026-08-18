import { dirname, resolve } from "path";
import { fileURLToPath } from "url";
import { defineConfig } from "vite";

const __dirname = dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  build: {
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
