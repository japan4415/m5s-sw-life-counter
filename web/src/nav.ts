/**
 * 共通ナビゲーション: 全ページに挿入されるヘッダーナビ
 */

interface NavItem {
  label: string;
  href: string;
}

const NAV_ITEMS: NavItem[] = [
  { label: "紹介", href: "/" },
  { label: "使い方", href: "/guide" },
  { label: "機能", href: "/features" },
  { label: "インストール", href: "/install" },
];

function getCurrentPath(): string {
  const path = location.pathname.replace(/\/index\.html$/, "");
  return path === "" ? "/" : path.replace(/\/$/, "") || "/";
}

function initNav(): void {
  const container = document.getElementById("global-nav");
  if (!container) return;

  container.setAttribute("aria-label", "メインナビゲーション");

  const currentPath = getCurrentPath();

  const inner = document.createElement("div");
  inner.className = "nav-inner";

  const brand = document.createElement("a");
  brand.href = "/";
  brand.className = "nav-brand";
  brand.textContent = "M5Stack StopWatch";
  inner.appendChild(brand);

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

  for (const item of NAV_ITEMS) {
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
