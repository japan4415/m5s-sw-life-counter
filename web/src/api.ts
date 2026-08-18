/**
 * Worker API クライアントとユーティリティ
 * - /api/releases の取得
 * - /api/firmware/:tag/:file の取得（バイナリ）
 * - SHA-256 計算・sha256sums.txt のパース
 */

export type Asset = {
  name: string;
  size: number;
  browser_download_url: string;
};

export type Release = {
  tag_name: string;
  name: string | null;
  body: string | null;
  prerelease: boolean;
  published_at: string | null;
  assets: Asset[];
};

type ApiErrorBody = { error?: string; detail?: string; status?: number };

/** リリース一覧を取得する（ブラウザキャッシュは無効化し、Worker 側キャッシュに一元管理） */
export async function fetchReleases(): Promise<Release[]> {
  const res = await fetch("/api/releases", { cache: "no-store" });
  if (!res.ok) {
    let detail = `HTTP ${res.status}`;
    try {
      const body = (await res.json()) as ApiErrorBody;
      if (body.error) detail = `${body.error}${body.detail ? `（${body.detail}）` : ""}`;
    } catch {
      // JSON でない場合は HTTP ステータスのみ
    }
    throw new Error(detail);
  }
  return (await res.json()) as Release[];
}

/** リリースアセット（ファームウェア・sha256sums.txt）をバイナリで取得する */
export async function fetchFirmwareFile(
  tag: string,
  file: string,
): Promise<Uint8Array<ArrayBuffer>> {
  const res = await fetch(
    `/api/firmware/${encodeURIComponent(tag)}/${encodeURIComponent(file)}`,
    { cache: "no-store" },
  );
  if (!res.ok) {
    let detail = `HTTP ${res.status}`;
    try {
      const body = (await res.json()) as ApiErrorBody;
      if (body.error) detail = body.detail ? `${body.error}（${body.detail}）` : body.error;
    } catch {
      // JSON でない場合は HTTP ステータスのみ
    }
    throw new Error(`ファームウェアの取得に失敗しました（${detail}）`);
  }
  return new Uint8Array(await res.arrayBuffer());
}

/** バイト列の SHA-256 を 16 進文字列で返す */
export async function sha256Hex(data: Uint8Array<ArrayBuffer>): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

/**
 * sha256sums.txt（sha256sum -b 相当: "<hash>  <filename>"、スペース 2 個区切り）を
 * filename -> 小文字ハッシュ の Map にパースする
 */
export function parseSha256Sums(text: string): Map<string, string> {
  const map = new Map<string, string>();
  for (const line of text.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    const parts = trimmed.split(/\s+/);
    if (parts.length >= 2 && /^[0-9a-fA-F]{64}$/.test(parts[0])) {
      map.set(parts[1], parts[0].toLowerCase());
    }
  }
  return map;
}

/** バイト数を人間可読な形式に整形する */
export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

/** ISO 日時を日本語表記に整形する */
export function formatDate(iso: string | null): string {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "—";
  return d.toLocaleDateString("ja-JP", {
    year: "numeric",
    month: "short",
    day: "numeric",
  });
}
