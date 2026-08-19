/**
 * 共通ナビゲーション: 全ページに挿入されるヘッダーナビ
 *
 * バリアント対応: URL パス（/fab/* または /edh/*）からバリアントを判定し、
 * ナビリンクをバリアントに応じて出し分ける。トップページ（/）ではバリアント
 * 非依存のリンクを表示する。
 */

/** ファームウェアバリアント */
export type FirmwareVariant = "fab" | "edh";

interface NavItem {
  label: string;
  href: string;
}

/** 現在の URL パスからバリアントを判定する。トップ・リダイレクトページでは null */
export function detectVariant(): FirmwareVariant | null {
  const path = location.pathname;
  if (path.startsWith("/fab/") || path === "/fab") return "fab";
  if (path.startsWith("/edh/") || path === "/edh") return "edh";
  return null;
}

const VARIANT_LABELS: Record<FirmwareVariant, string> = {
  fab: "for FaB",
  edh: "for MTG EDH",
};

function buildNavItems(variant: FirmwareVariant | null): NavItem[] {
  if (!variant) {
    // トップページ: バリアント選択前
    return [{ label: "紹介", href: "/" }];
  }
  const prefix = `/${variant}`;
  return [
    { label: "紹介", href: "/" },
    { label: "使い方", href: `${prefix}/guide` },
    { label: "機能", href: `${prefix}/features` },
    { label: "インストール", href: `${prefix}/install` },
  ];
}

function getCurrentPath(): string {
  const path = location.pathname.replace(/\/index\.html$/, "");
  return path === "" ? "/" : path.replace(/\/$/, "") || "/";
}

function initNav(): void {
  const container = document.getElementById("global-nav");
  if (!container) return;

  container.setAttribute("aria-label", "メインナビゲーション");

  const currentPath = getCurrentPath();
  const variant = detectVariant();
  const navItems = buildNavItems(variant);

  const inner = document.createElement("div");
  inner.className = "nav-inner";

  const brand = document.createElement("a");
  brand.href = "/";
  brand.className = "nav-brand";
  brand.textContent = "M5Stack StopWatch";
  inner.appendChild(brand);

  // バリアント表示 + 切り替えリンク
  if (variant) {
    const variantEl = document.createElement("span");
    variantEl.className = "nav-variant";

    const labelSpan = document.createElement("span");
    labelSpan.className = "nav-variant-label";
    labelSpan.textContent = VARIANT_LABELS[variant];
    variantEl.appendChild(labelSpan);

    const otherVariant: FirmwareVariant = variant === "fab" ? "edh" : "fab";
    const switchLink = document.createElement("a");
    switchLink.href = "/";
    switchLink.className = "nav-variant-switch";
    switchLink.textContent = "切替";
    switchLink.title = `${VARIANT_LABELS[otherVariant]} に切り替え`;
    variantEl.appendChild(switchLink);

    inner.appendChild(variantEl);
  }

  const toggle = document.createElement("button");
  toggle.className = "nav-toggle";
  toggle.setAttribute("aria-label", "メニューを開く");
  toggle.setAttribute("aria-expanded", "false");
  toggle.setAttribute("aria-controls", "nav-list");
  toggle.innerHTML = "<span></span><span></span><span></span>";
  inner.appendChild(toggle);

  const list = document.createElement("ul");
  list.className = "nav-list";
  list.id = "nav-list";

  for (const item of navItems) {
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = item.href;
    a.textContent = item.label;
    if (currentPath === item.href) {
      a.classList.add("nav-active");
      a.setAttribute("aria-current", "page");
    }
    li.appendChild(a);
    list.appendChild(li);
  }

  inner.appendChild(list);
  container.appendChild(inner);

  // モバイルメニュー開閉
  toggle.addEventListener("click", () => {
    const expanded = toggle.getAttribute("aria-expanded") === "true";
    toggle.setAttribute("aria-expanded", String(!expanded));
    list.classList.toggle("nav-open");
  });

  // スクロールエッジ: ページスクロール時に .is-scrolled をトグル
  const onScroll = (): void => {
    container.classList.toggle("is-scrolled", window.scrollY > 0);
  };
  window.addEventListener("scroll", onScroll, { passive: true });
  onScroll();
}

initNav();
