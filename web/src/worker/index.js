/**
 * M5Stack StopWatch ファームウェア書き込みアプリ用 Cloudflare Worker
 *
 * - GET /api/releases                : GitHub Releases 一覧のプロキシ（キャッシュ 5 分・フェイルオープン）
 * - GET /api/firmware/:tag/:file     : リリースアセット（バイナリ / sha256sums.txt）のプロキシ（キャッシュ 7 日）
 * - その他 /api/*                    : 404 JSON（ASSETS にはフォールバックしない）
 * - それ以外                          : 静的アセット配信（env.ASSETS）にフォールバック
 *
 * GitHub Releases のバイナリ（browser_download_url）は CORS ヘッダーを返さないため、
 * ブラウザから直接 fetch できない。サーバー側 fetch の中継によりバイナリ配信を可能にする。
 * 詳細は docs/14-web-flasher-design.md を参照。
 */

const RELEASES_CACHE_TTL = 5 * 60; // 5 分（新規リリースの反映遅延を抑える短 TTL）
const FIRMWARE_CACHE_TTL = 7 * 24 * 60 * 60; // 7 日（リリースは不変前提の長 TTL）

// Cache API はキーを URL として解釈するため、完全な URL 文字列をキーに使用する
// （"releases:list" のような文字列は TypeError になる）
const RELEASES_CACHE_KEY = "https://cache.local/releases";
const firmwareCacheKey = (tag, file) =>
  `https://cache.local/firmware/${encodeURIComponent(tag)}/${encodeURIComponent(file)}`;

// パス構成要素のバリデーション（[A-Za-z0-9._-]+ のみ許可）
const SAFE_PATH_RE = /^[A-Za-z0-9._-]+$/;

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8" },
  });
}

/** キャッシュ読み取り（キャッシュ失敗時は null を返し、GitHub 直 fetch へフォールバック） */
async function cacheGet(key) {
  try {
    return await caches.default.match(key);
  } catch {
    return null;
  }
}

/** キャッシュ書き込み（失敗しても無視: フェイルオープン維持） */
async function cachePut(key, response) {
  try {
    await caches.default.put(key, response.clone());
  } catch {
    // キャッシュ不可でも結果は返せるため握りつぶす
  }
}

/** リリース一覧を表示用フィールドのみに整形する（draft は除外） */
function shapeRelease(release) {
  return {
    tag_name: release.tag_name,
    name: release.name,
    body: release.body,
    prerelease: release.prerelease,
    published_at: release.published_at,
    assets: (Array.isArray(release.assets) ? release.assets : [])
      .map((a) => ({
        name: a.name,
        size: a.size,
        browser_download_url: a.browser_download_url,
      })),
  };
}

/** GET /api/releases: GitHub Releases 一覧のプロキシ */
async function handleReleases(env) {
  const cacheKey = RELEASES_CACHE_KEY;

  // キャッシュヒット時は即返す
  const cached = await cacheGet(cacheKey);
  if (cached) return cached;

  const headers = {
    "User-Agent": "m5s-sw-life-counter-flasher",
    Accept: "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
  };
  if (env.GITHUB_TOKEN) headers.Authorization = `Bearer ${env.GITHUB_TOKEN}`;

  let releases;
  try {
    const url = `https://api.github.com/repos/${env.GITHUB_REPO}/releases?per_page=20`;
    const res = await fetch(url, { headers });
    if (!res.ok) {
      throw new Error(`GitHub API responded with ${res.status}`);
    }
    releases = await res.json();
  } catch (err) {
    // フェイルオープン: GitHub エラー時はキャッシュがあれば返す
    const fallback = await cacheGet(cacheKey);
    if (fallback) return fallback;
    return json(
      {
        error: "リリース一覧を取得できませんでした",
        detail: err instanceof Error ? err.message : String(err),
      },
      502,
    );
  }

  const body = json(
    (Array.isArray(releases) ? releases : [])
      .filter((r) => !r.draft)
      .map(shapeRelease),
  );
  body.headers.set("Cache-Control", `public, max-age=${RELEASES_CACHE_TTL}`);
  await cachePut(cacheKey, body);
  return body;
}

/** GET /api/firmware/:tag/:file: リリースアセットのプロキシ */
async function handleFirmware(request, env, path) {
  // /api/firmware/:tag/:file を分解（tag/file は単一パス要素のみ許可）
  const rest = path.slice("/api/firmware/".length);
  const parts = rest.split("/");
  if (parts.length !== 2) {
    return json({ error: "不正なパスです" }, 400);
  }
  const [tag, file] = parts;
  const isSafe = (s) => SAFE_PATH_RE.test(s) && s !== "." && s !== "..";
  if (!isSafe(tag) || !isSafe(file)) {
    return json({ error: "不正なパスです" }, 400);
  }

  const cacheKey = firmwareCacheKey(tag, file);

  const cached = await cacheGet(cacheKey);
  if (cached) return cached;

  const url = `https://github.com/${env.GITHUB_REPO}/releases/download/${tag}/${file}`;
  let res;
  try {
    res = await fetch(url, { redirect: "follow" });
  } catch (err) {
    return json(
      {
        error: "ファームウェアの取得に失敗しました",
        detail: err instanceof Error ? err.message : String(err),
      },
      502,
    );
  }
  if (!res.ok) {
    // GitHub の応答ステータスをそのままクライアントに返す（リリース未作成の 404 は 404 のまま）
    return json({ error: "ファームウェアの取得に失敗しました", status: res.status }, res.status);
  }

  const body = new Response(res.body, {
    status: 200,
    headers: {
      "Content-Type": "application/octet-stream",
      "Cache-Control": `public, max-age=${FIRMWARE_CACHE_TTL}`,
    },
  });
  await cachePut(cacheKey, body);
  return body;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (request.method === "GET" && path === "/api/releases") {
      return handleReleases(env);
    }
    if (request.method === "GET" && path.startsWith("/api/firmware/")) {
      return handleFirmware(request, env, path);
    }
    // 未知の /api/* は 404 JSON を返す（ASSETS へフォールバックしない）
    if (path.startsWith("/api/")) {
      return json({ error: "Not Found" }, 404);
    }
    // 静的アセット配信（SPA のため not_found_handling で index.html にフォールバック）
    return env.ASSETS.fetch(request);
  },
};
