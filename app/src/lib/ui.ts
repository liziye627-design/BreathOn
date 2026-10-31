import { icon } from "./icons";
import type { Measurement, Reading } from "./api";

export function $<T extends HTMLElement>(sel: string, root: ParentNode = document): T {
  return root.querySelector(sel) as T;
}

const ESC: Record<string, string> = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };
export const esc = (s: string) => s.replace(/[&<>"']/g, (c) => ESC[c]);
export const pad = (n: number) => String(n).padStart(2, "0");

export function toast(msg: string) {
  document.querySelector(".toast")?.remove();
  const t = document.createElement("div");
  t.className = "toast";
  t.setAttribute("role", "status");
  t.textContent = msg;
  document.body.append(t);
  setTimeout(() => t.remove(), 2400);
}

export function modal(html: string): { el: HTMLElement; close: () => void } {
  const el = document.createElement("div");
  el.className = "modal";
  el.innerHTML = `<div class="sheet" role="dialog" aria-modal="true">${html}</div>`;
  const close = () => el.remove();
  el.addEventListener("click", (e) => { if (e.target === el) close(); });
  document.body.append(el);
  return { el, close };
}

export function topbar(opts: { back?: string; settings?: boolean } = {}): string {
  const left = opts.back
    ? `<button class="round-btn" data-go="${opts.back}" aria-label="返回">${icon("back")}</button>`
    : `<img class="brand" src="/brand/brandlockup_horizontal.svg" alt="灵息 Breath On">`;
  const gear = opts.settings ? `<button class="round-btn" data-go="profile" aria-label="设置">${icon("settings")}</button>` : "";
  return `<div class="topbar">${left}<div class="top-actions">${gear}<button class="round-btn" data-toast="暂无新通知" aria-label="通知">${icon("bell")}</button></div></div>`;
}

export function heroHead(title: string, lead: string, mascot = "idle"): string {
  return `<div class="hero-head"><div><h1>${title}</h1><p class="lead">${lead}</p></div>
    <img class="mascot" src="/mascot/orange_device_${mascot}.svg" alt=""></div>`;
}

/** 来源标签：mock 必须显示"工程测试输入 · 非实测"，不可省略。 */
export function sourceTag(m: Pick<Reading, "source"> | null): string {
  if (!m) return `<span class="tag none">暂无数据</span>`;
  return m.source === "mock"
    ? `<span class="tag mock">${icon("alert")}工程测试输入 · 非实测</span>`
    : `<span class="tag sensor">传感器输入</span>`;
}

export function calibrationText(m: Pick<Reading, "calibration">): string {
  return { uncalibrated: "未校准", verified: "已校准", not_applicable: "不适用" }[m.calibration];
}

export function qualityText(m: Pick<Reading, "quality">): string {
  return { ok: "正常", suspect: "存疑", invalid: "无效", unknown: "未评估" }[m.quality];
}

export const fmtPpm = (v: number | null | undefined) => (v == null ? "--" : v.toFixed(1));
export const fmtDate = (ts: number) => {
  const d = new Date(ts * 1000);
  return `${d.getMonth() + 1}月${d.getDate()}日 ${pad(d.getHours())}:${pad(d.getMinutes())}`;
};

export function last(rows: Measurement[]): Measurement | null {
  return rows.length ? rows[rows.length - 1] : null;
}

/** 事件委托：data-go 跳转，data-toast 弹提示。 */
export function bindGlobalClicks(go: (to: string) => void) {
  document.addEventListener("click", (e) => {
    const t = e.target as HTMLElement;
    const g = t.closest<HTMLElement>("[data-go]");
    if (g) return go(g.dataset.go!);
    const m = t.closest<HTMLElement>("[data-toast]");
    if (m) toast(m.dataset.toast!);
  });
}
