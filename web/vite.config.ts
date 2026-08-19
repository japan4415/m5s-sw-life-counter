import { dirname, resolve } from "path";
import { fileURLToPath } from "url";
import { existsSync } from "fs";
import { defineConfig } from "vite";
import type { Plugin } from "vite";

const __dirname = dirname(fileURLToPath(import.meta.url));

/**
 * dev サーバーで clean URL を trailing-slash 付きにリダイレクトする Vite プラグイン。
 * 本番環境では Cloudflare assets の html_handling = "auto-trailing-slash" が同等の処理を行う。
 */
function autoTrailingSlash(): Plugin {
  return {
    name: "auto-trailing-slash",
    configureServer(server) {
      server.middlewares.use((req, _res, next) => {
        const url = req.url;
        if (!url || url.startsWith("//") || url.startsWith("/@") || url.startsWith("/__")) {
          return next();
        }

        // クエリ・ハッシュをパス部分から分離する
        const qIdx = url.indexOf("?");
        const hIdx = url.indexOf("#");
        let suffixStart = -1;
        if (qIdx !== -1 && hIdx !== -1) suffixStart = Math.min(qIdx, hIdx);
        else if (qIdx !== -1) suffixStart = qIdx;
        else if (hIdx !== -1) suffixStart = hIdx;

        const pathname = suffixStart === -1 ? url : url.slice(0, suffixStart);
        const suffix = suffixStart === -1 ? "" : url.slice(suffixStart);

        if (pathname.endsWith("/") || pathname.includes(".")) {
          return next();
        }

        // パストラバーサル防止: 解決後のパスがプロジェクトルート配下であることを確認
        const dirPath = resolve(__dirname, pathname.slice(1));
        if (!dirPath.startsWith(__dirname)) {
          return next();
        }

        // ディレクトリに index.html が存在する場合のみリダイレクト
        if (existsSync(resolve(dirPath, "index.html"))) {
          const res = _res;
          res.writeHead(301, { Location: pathname + "/" + suffix });
          res.end();
          return;
        }
        next();
      });
    },
  };
}

export default defineConfig({
  // MPA モード: dev サーバーの SPA フォールバックを無効化する
  appType: "mpa",
  plugins: [autoTrailingSlash()],
  build: {
    // Vite 8 のデフォルト CSS minifier は -webkit- 付き宣言があると標準の
    // backdrop-filter 宣言を除去してしまう。esbuild は両方保持するため明示指定する。
    cssMinify: "esbuild",
    outDir: "dist",
    rollupOptions: {
      input: {
        main: resolve(__dirname, "index.html"),
        // リダイレクトページ（旧 URL → /fab/* へ転送）
        install: resolve(__dirname, "install/index.html"),
        guide: resolve(__dirname, "guide/index.html"),
        features: resolve(__dirname, "features/index.html"),
        // FaB バリアント
        fabInstall: resolve(__dirname, "fab/install/index.html"),
        fabGuide: resolve(__dirname, "fab/guide/index.html"),
        fabFeatures: resolve(__dirname, "fab/features/index.html"),
        // EDH バリアント
        edhInstall: resolve(__dirname, "edh/install/index.html"),
        edhGuide: resolve(__dirname, "edh/guide/index.html"),
        edhFeatures: resolve(__dirname, "edh/features/index.html"),
        // 404
        notFound: resolve(__dirname, "404.html"),
      },
    },
  },
});
