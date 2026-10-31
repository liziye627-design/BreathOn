// 内联设计包里的 SVG 图标，使其继承 currentColor。
const files = import.meta.glob("../assets/icons/*.svg", { query: "?raw", import: "default", eager: true }) as Record<string, string>;

const cache: Record<string, string> = {};
for (const [path, raw] of Object.entries(files)) {
  const name = path.split("/").pop()!.replace(".svg", "");
  // 只处理根 <svg> 标签：去掉固定宽高和 aria 标注，保留内部 rect 等元素的尺寸属性。
  cache[name] = raw.replace(/<svg[^>]*>/, (tag) =>
    tag.replace(/\s(width|height)="[^"]*"/g, "").replace(/\srole="img"/, "").replace(/\saria-label="[^"]*"/, "").replace("<svg", '<svg aria-hidden="true"'));
}

export type IconName =
  | "alert" | "arrow_right" | "back" | "battery" | "bell" | "bluetooth" | "bot" | "calendar" | "camera" | "check"
  | "check_circle" | "chevron_right" | "device" | "droplet" | "heart" | "home" | "lock" | "mic" | "more" | "pill"
  | "plus" | "run" | "settings" | "trend" | "upload" | "user" | "utensils" | "wave" | "weight" | "wifi" | "wind";

export function icon(name: IconName, cls = ""): string {
  return `<span class="ic ${cls}">${cache[name] ?? ""}</span>`;
}
