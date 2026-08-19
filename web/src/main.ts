/**
 * M5Stack StopWatch（ESP32-S3）ファームウェア書き込みウィザード
 *
 * 6 ステップ構成（docs/14-web-flasher-design.md §4 準拠）:
 *   1. リリース選択  2. ダウンロードモード移行  3. 接続・ポート選択
 *   4. チップ認識    5. 書き込み               6. 起動確認
 *
 * 接続戦略: ユーザーが電源ボタン長押しでダウンロードモードに入っている前提のため、
 * 接続は main("no_reset")、書き込み後も after("no_reset") でリセットせず、
 * 電源ボタン短押しによる手動リセットを案内する。
 *
 * NOTE: このモジュールは install.ts 経由で読み込まれる想定です（nav の注入を含む）。
 * install/index.html から直接 /src/main.ts を参照しないでください。
 */
import { ESPLoader, Transport } from "esptool-js";
import type { LoaderOptions } from "esptool-js";
import "./style.css";
import {
  fetchReleases,
  fetchFirmwareFile,
  sha256Hex,
  parseSha256Sums,
  formatBytes,
  formatDate,
} from "./api";
import type { Release } from "./api";

// ---- 書き込み構成（docs/14-web-flasher-design.md §2・§5） ----
const FIRMWARE_FILES_FULL = [
  { name: "bootloader.bin", address: 0x0 },
  { name: "partitions.bin", address: 0x8000 },
  { name: "boot_app0.bin", address: 0xe000 },
  { name: "firmware.bin", address: 0x10000 },
];
const FIRMWARE_FILES_UPDATE = [{ name: "firmware.bin", address: 0x10000 }];
const FLASH_MODE = "keep" as const;
const FLASH_FREQ = "keep" as const;
const FLASH_SIZE = "keep" as const;
const EXPECTED_CHIP = "ESP32-S3";
const SHA256S_SUMS = "sha256sums.txt";

type FlashMode = "update" | "full";

type FlashFile = {
  name: string;
  address: number;
  size: number;
  assetPresent: boolean;
  expectedHash: string | null;
};

// ---- アプリ状態 ----
const state = {
  selected: null as Release | null,
  hashes: new Map<string, string>(), // filename -> sha256（sha256sums.txt 由来）
  hasShaFile: false,
  mode: "update" as FlashMode,
  port: null as SerialPort | null,
  loader: null as ESPLoader | null,
  flashing: false,
};

// ---- DOM ヘルパー ----
function el<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (!node) throw new Error(`missing element: #${id}`);
  return node as T;
}

function showError(boxId: string, titleId: string, hintId: string, title: string, hint: string) {
  el(boxId).hidden = false;
  el(titleId).textContent = title;
  el(hintId).textContent = hint;
}

function hideError(boxId: string) {
  el(boxId).hidden = true;
}

// ---- ステップ管理 ----
const TOTAL_STEPS = 6;

function showStep(n: number) {
  for (let i = 1; i <= TOTAL_STEPS; i++) {
    el(`step-${i}`).hidden = i !== n;
  }
  document.querySelectorAll<HTMLElement>(".step-item").forEach((item) => {
    const s = Number(item.dataset.step);
    item.classList.toggle("active", s === n);
    item.classList.toggle("done", s < n);
  });
  window.scrollTo({ top: 0, behavior: "smooth" });
}

// ---- ステップ 1: リリース選択 ----
async function loadReleases() {
  const list = el<HTMLDivElement>("release-list");
  list.innerHTML = "";
  hideError("release-error");
  el<HTMLButtonElement>("step1-next").disabled = true;
  el("release-detail").hidden = true;
  state.selected = null;
  state.hashes.clear();
  state.hasShaFile = false;

  let releases: Release[];
  try {
    releases = await fetchReleases();
  } catch (err) {
    showError(
      "release-error",
      "release-error-text",
      "release-error-hint",
      "リリース一覧を取得できませんでした。",
      "ネットワーク接続を確認し、再試行してください。初回セットアップはリポジトリの CI で v* タグの Release を作成する必要があります（キャッシュがあれば表示されます）。",
    );
    return;
  }

  if (releases.length === 0) {
    const empty = document.createElement("p");
    empty.className = "warn-box";
    empty.textContent =
      "リリースがまだありません。初回セットアップはリポジトリの CI で v* タグの Release を作成する必要があります。";
    list.appendChild(empty);
    return;
  }

  // 既定 = 最新の安定版（prerelease でない最初のリリース）。安定版 0 件の場合は明示選択を要求
  const defaultRelease = releases.find((r) => !r.prerelease) ?? null;
  for (const release of releases) {
    list.appendChild(createReleaseCard(release, release === defaultRelease));
  }
  if (defaultRelease) {
    await selectRelease(defaultRelease);
  } else {
    const note = document.createElement("p");
    note.className = "muted small";
    note.textContent =
      "安定版（正式リリース）がまだありません。試験版（prerelease）から明示的に選択してください。";
    list.prepend(note);
  }
}

function createReleaseCard(release: Release, isDefault: boolean): HTMLButtonElement {
  const card = document.createElement("button");
  card.type = "button";
  card.className = "release-card";
  card.dataset.tag = release.tag_name;
  if (isDefault) card.classList.add("selected");

  const head = document.createElement("div");
  head.className = "release-head";
  const tag = document.createElement("span");
  tag.className = "release-tag";
  tag.textContent = release.tag_name;
  const badge = document.createElement("span");
  badge.className = release.prerelease ? "badge badge-prerelease" : "badge badge-stable";
  badge.textContent = release.prerelease ? "試験版（prerelease）" : "安定版";
  const date = document.createElement("span");
  date.className = "release-date";
  date.textContent = formatDate(release.published_at);
  head.append(tag, badge, date);

  const notes = document.createElement("p");
  notes.className = "release-notes";
  notes.textContent = release.body?.trim() || release.name || "";
  if (!notes.textContent) notes.hidden = true;

  card.append(head, notes);
  card.addEventListener("click", () => {
    void selectRelease(release);
  });
  return card;
}

async function selectRelease(release: Release) {
  state.selected = release;
  document.querySelectorAll<HTMLElement>(".release-card").forEach((c) => {
    c.classList.toggle("selected", c.dataset.tag === release.tag_name);
  });
  el<HTMLButtonElement>("step1-next").disabled = false;
  el("detail-tag").textContent = release.tag_name;
  el("detail-meta").textContent =
    `${release.prerelease ? "試験版（prerelease）" : "安定版"}・公開: ${formatDate(release.published_at)}`;
  renderAssets(release);
  await loadHashes(release.tag_name);
  el("release-detail").hidden = false;
}

function renderAssets(release: Release) {
  const box = el<HTMLDivElement>("detail-assets");
  box.innerHTML = "";
  const targets = [...FIRMWARE_FILES_FULL.map((f) => f.name), SHA256S_SUMS];
  for (const name of targets) {
    const asset = release.assets.find((a) => a.name === name);
    const row = document.createElement("div");
    row.className = "asset-row";
    const nameEl = document.createElement("span");
    nameEl.className = "asset-name";
    nameEl.textContent = asset ? name : `${name}（未添付）`;
    const meta = document.createElement("span");
    meta.className = "asset-meta";
    meta.textContent = asset ? formatBytes(asset.size) : "—";
    row.append(nameEl, meta);
    box.appendChild(row);
  }
}

/** sha256sums.txt を取得してハッシュマップを構築する（失敗時は hasShaFile=false のまま） */
async function loadHashes(tag: string) {
  state.hashes.clear();
  state.hasShaFile = false;
  const hashList = el<HTMLParagraphElement>("detail-hashes");
  try {
    const data = await fetchFirmwareFile(tag, SHA256S_SUMS);
    const map = parseSha256Sums(new TextDecoder().decode(data));
    if (map.size === 0) throw new Error("empty sha256sums.txt");
    state.hashes = map;
    state.hasShaFile = true;
    const lines = [...map.entries()].map(([file, hash]) => `${file}: ${hash}`);
    hashList.textContent = lines.join("\n");
    hashList.classList.add("hash-ok");
  } catch {
    hashList.textContent =
      "SHA-256 の照合元（sha256sums.txt）を取得できませんでした。書き込み前に再確認されます。";
    hashList.classList.remove("hash-ok");
  }
}

// ---- ステップ 3: 接続・ポート選択 ----
function describePort(port: SerialPort): string {
  const info = port.getInfo();
  const parts: string[] = [];
  if (info.usbVendorId != null) {
    parts.push(`VID:0x${info.usbVendorId.toString(16).padStart(4, "0")}`);
  }
  if (info.usbProductId != null) {
    parts.push(`PID:0x${info.usbProductId.toString(16).padStart(4, "0")}`);
  }
  return parts.join(" / ") || "シリアルポート";
}

async function restorePortHint() {
  const area = el<HTMLDivElement>("connect-area");
  area.querySelectorAll("button[data-restore]").forEach((b) => b.remove());
  try {
    const ports = await navigator.serial.getPorts();
    if (ports.length === 0) return;
    const btn = document.createElement("button");
    btn.type = "button";
    btn.dataset.restore = "1";
    btn.className = "btn";
    btn.textContent = "以前に接続したポートを再利用";
    btn.addEventListener("click", () => {
      state.port = ports[0];
      el("connect-hint").textContent =
        "以前のポートを再利用します（別のデバイスを選ぶ場合は「接続してポートを選択」を押してください）。";
      el<HTMLButtonElement>("step3-next").disabled = false;
      btn.disabled = true;
    });
    area.appendChild(btn);
  } catch {
    // getPorts 失敗時は通常の接続フローのみ
  }
}

// ---- ステップ 4: チップ認識 ----
function createLoader(port: SerialPort): ESPLoader {
  const options: LoaderOptions = {
    transport: new Transport(port),
    baudrate: 921600,
    terminal: {
      clean: () => {},
      write: (data: string) => {
        if (import.meta.env.DEV) console.log("[esptool]", data.trimEnd());
      },
      writeLine: (data: string) => {
        if (import.meta.env.DEV) console.log("[esptool]", data.trimEnd());
      },
    },
  };
  const loader = new ESPLoader(options);

  // 保険的実装: ポート喪失（USB 再列挙など）を検知したら再取得して差し替える。
  // 接続時に保存した port オブジェクトを優先し、無ければ列挙の先頭を使う
  loader.transport.setDeviceLostCallback(() => {
    void (async () => {
      try {
        const ports = await navigator.serial.getPorts();
        const preferred = ports.find((p) => p === state.port) ?? ports[0];
        if (preferred) {
          loader.transport.updateDevice(preferred); // 同期メソッド（Promise ではない、v0.6.1 型定義確認済み）
          if (import.meta.env.DEV) console.log("[esptool] device lost -> updated");
        }
      } catch {
        // 復帰不能なら放置（エラーフローで最初からやり直しを案内）
      }
    })();
  });
  return loader;
}

async function detectChip() {
  const status = el("chip-status");
  const result = el("chip-result");
  const warn = el("chip-warn");
  hideError("chip-error");
  result.hidden = true;
  warn.hidden = true;
  status.hidden = false;
  el<HTMLButtonElement>("step4-next").disabled = true;

  if (!state.port) {
    showStep(3);
    return;
  }

  try {
    const loader = createLoader(state.port);
    state.loader = loader;
    // ダウンロードモード（電源ボタン長押し済み）前提のためリセットを一切実行しない
    await loader.main("no_reset");
    const chip = loader.chip;
    if (!chip) {
      throw new Error("チップ情報が取得できませんでした");
    }
    status.hidden = true;
    result.hidden = false;
    el("chip-name").textContent = chip.CHIP_NAME;
    el("chip-info").textContent = await chip.getChipDescription(loader);
    el("chip-release").textContent = state.selected?.tag_name ?? "（未選択）";

    if (chip.CHIP_NAME !== EXPECTED_CHIP) {
      warn.hidden = false;
    } else {
      el<HTMLButtonElement>("step4-next").disabled = false;
    }
  } catch (err) {
    status.hidden = true;
    const detail = err instanceof Error ? `（詳細: ${err.message}）` : "";
    showError(
      "chip-error",
      "chip-error-text",
      "chip-error-hint",
      "チップを認識できませんでした。",
      `ダウンロードモードに入らず、アプリ側の USB-CDC（PID 0x303A）に接続した可能性があります。ステップ②に戻り、電源ボタン約 2 秒長押し（緑 LED 点灯）でダウンロードモードに切り替えてから再試行してください。${detail}`,
    );
  }
}

/** ポートを解放し、他のツール・タブで使える状態に戻す */
async function disconnectLoader() {
  const loader = state.loader;
  if (loader) {
    try {
      await loader.transport.disconnect();
    } catch {
      // 既に閉じている場合は無視
    }
    state.loader = null;
  }
  state.port = null;
}

// ---- ステップ 5: 書き込み ----
function buildFlashFiles(mode: FlashMode): FlashFile[] {
  const spec = mode === "full" ? FIRMWARE_FILES_FULL : FIRMWARE_FILES_UPDATE;
  const release = state.selected;
  return spec.map((f) => {
    const asset = release?.assets.find((a) => a.name === f.name) ?? null;
    return {
      name: f.name,
      address: f.address,
      size: asset?.size ?? 0,
      assetPresent: asset != null,
      expectedHash: state.hashes.get(f.name) ?? null,
    };
  });
}

function renderFlashSummary() {
  const files = buildFlashFiles(state.mode);
  const box = el<HTMLDivElement>("flash-summary");
  box.innerHTML = "";
  let total = 0;
  for (const f of files) {
    const row = document.createElement("div");
    row.className = "asset-row";
    const nameEl = document.createElement("span");
    nameEl.className = "asset-name";
    nameEl.textContent = `${f.name}  @0x${f.address.toString(16)}`;
    const meta = document.createElement("span");
    meta.className = "asset-meta";
    meta.textContent = f.assetPresent ? formatBytes(f.size) : "（未添付）";
    total += f.size;
    row.append(nameEl, meta);
    box.appendChild(row);
  }
  const totalRow = document.createElement("p");
  totalRow.className = "muted small";
  totalRow.textContent = `合計: ${formatBytes(total)}`;
  box.appendChild(totalRow);
}

/** 書き込み前確認モーダル（チップ・ファイル一覧・SHA-256・合計サイズ） */
function showConfirmModal(files: FlashFile[]): Promise<boolean> {
  const modal = el("confirm-modal");
  el("confirm-chip-line").textContent =
    `対象デバイス: ${state.loader?.chip.CHIP_NAME ?? "—"}（期待: ${EXPECTED_CHIP}）/ リリース: ${state.selected?.tag_name ?? "—"} / モード: ${state.mode === "full" ? "フルフラッシュ（全消去）" : "アプリ更新"}`;
  const box = el<HTMLDivElement>("confirm-files");
  box.innerHTML = "";
  let total = 0;
  for (const f of files) {
    const row = document.createElement("div");
    row.className = "asset-row";
    const nameEl = document.createElement("span");
    nameEl.className = "asset-name";
    nameEl.textContent = `${f.name}  @0x${f.address.toString(16)}`;
    const meta = document.createElement("span");
    meta.className = "asset-meta";
    meta.textContent = `${formatBytes(f.size)} / SHA-256: ${f.expectedHash ?? "照合できません"}`;
    total += f.size;
    row.append(nameEl, meta);
    box.appendChild(row);
  }
  el("confirm-total").textContent = `合計: ${formatBytes(total)}`;
  el("confirm-nvs-warn").hidden = state.mode !== "full";
  el("confirm-hash-warn").hidden = state.hasShaFile;

  const okBtn = el<HTMLButtonElement>("confirm-ok");
  const cancelBtn = el<HTMLButtonElement>("confirm-cancel");
  if (state.hasShaFile) {
    okBtn.textContent = "書き込みを実行";
    okBtn.classList.remove("btn-danger");
  } else {
    // 照合元が無い場合は非推奨であることを強調（初回実機検証用の保険としてのみ続行を許可）
    okBtn.textContent = "SHA-256 照合なしで書き込みを実行（非推奨）";
    okBtn.classList.add("btn-danger");
  }

  return new Promise((resolve) => {
    modal.hidden = false;
    const onKeydown = (e: KeyboardEvent) => {
      if (e.key === "Escape") close(false);
    };
    const onOverlayClick = (e: MouseEvent) => {
      if (e.target === modal) close(false);
    };
    const close = (result: boolean) => {
      modal.hidden = true;
      okBtn.onclick = null;
      cancelBtn.onclick = null;
      document.removeEventListener("keydown", onKeydown);
      modal.removeEventListener("click", onOverlayClick);
      resolve(result);
    };
    okBtn.onclick = () => close(true);
    cancelBtn.onclick = () => close(false);
    document.addEventListener("keydown", onKeydown);
    modal.addEventListener("click", onOverlayClick);
  });
}

function setProgress(pct: number, text: string) {
  el("progress-bar").style.width = `${Math.max(0, Math.min(100, pct))}%`;
  el("progress-text").textContent = text;
}

async function runFlash(files: FlashFile[]) {
  const loader = state.loader;
  const release = state.selected;
  if (!loader || !release) return;

  state.flashing = true;
  hideError("flash-error");
  el<HTMLButtonElement>("flash-btn").disabled = true;
  el<HTMLButtonElement>("step5-back").disabled = true;
  document
    .querySelectorAll<HTMLInputElement>('input[name="flash-mode"]')
    .forEach((r) => (r.disabled = true));
  el("progress-area").hidden = false;
  setProgress(0, "準備中…");

  try {
    // 1) バイナリ取得 + SHA-256 自動照合（不一致なら書き込み中止）
    const fileArray: { data: Uint8Array; address: number }[] = [];
    for (const f of files) {
      setProgress(0, `ダウンロード中: ${f.name} …`);
      const data = await fetchFirmwareFile(release.tag_name, f.name);
      if (f.expectedHash) {
        const actual = await sha256Hex(data);
        if (actual !== f.expectedHash) {
          throw new Error(
            `${f.name} の SHA-256 が一致しません（配布物の破損・改ざんの可能性があります）。再試行してください。`,
          );
        }
      }
      fileArray.push({ data, address: f.address });
    }

    // 2) 書き込み
    await loader.writeFlash({
      fileArray,
      flashMode: FLASH_MODE,
      flashFreq: FLASH_FREQ,
      flashSize: FLASH_SIZE,
      eraseAll: state.mode === "full",
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const prevBytes = fileArray
          .slice(0, fileIndex)
          .reduce((sum, fa) => sum + fa.data.length, 0);
        const totalBytes = fileArray.reduce((sum, fa) => sum + fa.data.length, 0);
        const done = prevBytes + written;
        const pct = totalBytes === 0 ? 0 : Math.round((done / totalBytes) * 100);
        setProgress(
          pct,
          `${fileIndex + 1}/${fileArray.length} ファイル目: ${formatBytes(written)} / ${formatBytes(total)}（全体 ${pct}%）`,
        );
      },
    });

    // 3) リセットは行わない（自動リセットのみでは起動しないことがあるため電源ボタン短押しで起動を案内）
    await loader.after("no_reset");

    // 4) ポート解放（他のツール・タブで使える状態に戻す）
    await disconnectLoader();

    setProgress(100, "書き込みが完了しました。");
    el<HTMLButtonElement>("step5-next").disabled = false;
  } catch (err) {
    // 不完全な状態のポートを解放し、ステップ③からの再接続に誘導する
    await disconnectLoader();
    const detail = err instanceof Error ? `（詳細: ${err.message}）` : "";
    showError(
      "flash-error",
      "flash-error-text",
      "flash-error-hint",
      "書き込みに失敗しました。",
      `ケーブルが抜けていないか・タブを閉じていないかを確認し、ステップ③からポートを再接続して最初からやり直してください。${detail}`,
    );
  } finally {
    state.flashing = false;
    el<HTMLButtonElement>("flash-btn").disabled = false;
    el<HTMLButtonElement>("step5-back").disabled = false;
    document
      .querySelectorAll<HTMLInputElement>('input[name="flash-mode"]')
      .forEach((r) => (r.disabled = false));
  }
}

// ---- 初期化 ----
function bindNavigation() {
  el("step1-next").addEventListener("click", () => showStep(2));
  el("reload-releases").addEventListener("click", () => {
    void loadReleases();
  });
  el("step2-back").addEventListener("click", () => showStep(1));
  el("step2-next").addEventListener("click", () => {
    showStep(3);
    void restorePortHint();
  });
  el("step3-back").addEventListener("click", () => showStep(2));
  el("step3-next").addEventListener("click", () => {
    showStep(4);
    void detectChip();
  });
  el("step4-back").addEventListener("click", () => {
    // 接続状態をリセットしてからステップ③へ戻る（再訪時にポート再選択を促す）
    void disconnectLoader();
    el<HTMLButtonElement>("step3-next").disabled = true;
    el("connect-hint").textContent = "";
    showStep(3);
  });
  el("step4-next").addEventListener("click", () => {
    el("flash-chip").textContent = state.loader?.chip.CHIP_NAME ?? "—";
    el("flash-release").textContent = state.selected?.tag_name ?? "—";
    showStep(5);
    renderFlashSummary();
  });
  el("step5-back").addEventListener("click", () => showStep(4));
  el("step5-next").addEventListener("click", () => {
    el("done-version").textContent = state.selected?.tag_name ?? "—";
    showStep(6);
  });
  el("step6-back").addEventListener("click", () => showStep(5));
  el("step6-restart").addEventListener("click", () => {
    void disconnectLoader();
    showStep(1);
    void loadReleases();
  });
}

function bindConnect() {
  el("connect-btn").addEventListener("click", async () => {
    hideError("connect-error");
    try {
      // requestPort はユーザージェスチャ内でのみ呼ぶ（フィルタなし・全ポート表示）
      const port = await navigator.serial.requestPort();
      state.port = port;
      el("connect-hint").textContent = `接続しました: ${describePort(port)}`;
      el<HTMLButtonElement>("step3-next").disabled = false;
    } catch (err) {
      if (err instanceof DOMException && err.name === "NotFoundError") {
        el("connect-hint").textContent = "ポート選択がキャンセルされました。";
      } else {
        showError(
          "connect-error",
          "connect-error-text",
          "connect-error-hint",
          "ポートに接続できませんでした。",
          "ほかのタブやシリアルモニタがポートを占有している可能性があります。それらを閉じてから再試行してください。",
        );
      }
    }
  });
}

function bindFlashMode() {
  document.querySelectorAll<HTMLInputElement>('input[name="flash-mode"]').forEach((radio) => {
    radio.addEventListener("change", () => {
      state.mode = radio.value === "full" ? "full" : "update";
      el("erase-warn").hidden = state.mode !== "full";
      renderFlashSummary();
    });
  });
  el("flash-btn").addEventListener("click", async () => {
    hideError("flash-error");
    const files = buildFlashFiles(state.mode);
    const missing = files.filter((f) => !f.assetPresent);
    if (missing.length > 0) {
      const missingNames = missing.map((f) => f.name);
      const isBootApp0Only =
        missingNames.length === 1 && missingNames[0] === "boot_app0.bin";
      showError(
        "flash-error",
        "flash-error-text",
        "flash-error-hint",
        isBootApp0Only
          ? "このリリースはフルフラッシュに対応していません。"
          : "書き込み対象のファイルがリリースに含まれていません。",
        isBootApp0Only
          ? "フルフラッシュに必要な boot_app0.bin がリリースに含まれていません。「アプリ更新」モードを使用するか、boot_app0.bin が含まれるリリースを選択してください。"
          : `リリース構成（bootloader.bin / partitions.bin / boot_app0.bin / firmware.bin / ${SHA256S_SUMS}）を確認してください。欠落: ${missingNames.join(", ")}`,
      );
      return;
    }
    const ok = await showConfirmModal(files);
    if (ok) await runFlash(files);
  });
}

function bindBeforeUnload() {
  window.addEventListener("beforeunload", (e) => {
    if (state.flashing) {
      e.preventDefault();
      e.returnValue = "";
    }
  });
}

function init() {
  // Web Serial 非対応ブラウザ（Safari / Firefox / Android Chrome 等）には非対応メッセージを表示
  if (!("serial" in navigator)) {
    el("unsupported").hidden = false;
    document.querySelectorAll<HTMLElement>(".step-panel").forEach((p) => {
      if (p.id !== "unsupported") p.hidden = true;
    });
    document.querySelectorAll(".stepper").forEach((s) => ((s as HTMLElement).hidden = true));
    return;
  }

  bindNavigation();
  bindConnect();
  bindFlashMode();
  bindBeforeUnload();
  showStep(1);
  void loadReleases();
}

init();
