import "./styles/base.css";
import "./styles/pages.css";
import "./styles/ref.css";
import { icon, IconName } from "./lib/icons";
import { bindGlobalClicks } from "./lib/ui";
import { api } from "./lib/api";
import { renderWelcome } from "./views/welcome";
import { renderHome } from "./views/home";
import { renderDevice } from "./views/device";
import { renderMeasuring } from "./views/measuring";
import { renderResult } from "./views/result";
import { renderTrends } from "./views/trends";
import { renderRecords } from "./views/records";
import { renderChat } from "./views/chat";
import { showSplash } from "./views/splash";
import { renderProfile } from "./views/profile";

export type Route = "welcome" | "home" | "device" | "measuring" | "result" | "trends" | "records" | "chat" | "profile";
type View = (el: HTMLElement) => void | Promise<void>;

const VIEWS: Record<Route, View> = {
  welcome: renderWelcome, home: renderHome, device: renderDevice, measuring: renderMeasuring,
  result: renderResult, trends: renderTrends, records: renderRecords, chat: renderChat, profile: renderProfile,
};

const TABS: { id: Route; ico: IconName; label: string }[] = [
  { id: "home", ico: "home", label: "首页" },
  { id: "records", ico: "calendar", label: "记录" },
  { id: "trends", ico: "trend", label: "趋势" },
  { id: "chat", ico: "bot", label: "AI伙伴" },
  { id: "profile", ico: "user", label: "我的" },
];
const FLOW: Route[] = ["welcome", "device", "measuring", "result"];

const app = document.getElementById("app")!;
const page = document.createElement("main");
const nav = document.createElement("nav");
nav.className = "bottom";
nav.setAttribute("aria-label", "主导航");
app.append(page, nav);

export async function go(route: Route) {
  const flow = FLOW.includes(route);
  nav.hidden = flow;
  nav.innerHTML = TABS.map(
    (t) => `<button class="nav${t.id === route ? " active" : ""}" data-go="${t.id}" aria-label="${t.label}"${t.id === route ? ' aria-current="page"' : ""}>${icon(t.ico)}${t.label}</button>`,
  ).join("");
  page.className = "screen" + (flow ? " flow" : "");
  page.innerHTML = "";
  try {
    await VIEWS[route](page);
  } catch (e) {
    page.innerHTML = `<div class="card"><h3 class="card-title">页面加载失败</h3><p class="card-sub">${String(e)}</p></div>`;
  }
  window.scrollTo(0, 0);
}

bindGlobalClicks((to) => void go(to as Route));

showSplash();

(async () => {
  const seen = await api.getSetting("onboarded");
  void go(seen === "1" ? "home" : "welcome");
})();
