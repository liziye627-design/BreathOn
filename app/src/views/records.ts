import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";

export async function renderRecords(el: HTMLElement) {
  const records = await api.records("30d");
  const dayTs = Math.floor(Date.now() / 1000);
  const { logs } = await api.logsForDay(dayTs);

  el.className = "page records";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>健康记录</h1>
    </header>

    <div class="records-hero">
      <img src="/ref/records_hero.png" alt="">
    </div>

    <section class="quick-log">
      <h2>快速记录</h2>
      <div class="log-grid">
        <button class="log-btn" data-kind="meal">${icon("utensils")} 饮食</button>
        <button class="log-btn" data-kind="activity">${icon("run")} 运动</button>
        <button class="log-btn" data-kind="water">${icon("droplet")} 饮水</button>
        <button class="log-btn" data-kind="weight">${icon("weight")} 体重</button>
      </div>
    </section>

    <section class="records-list">
      <h2>最近检测</h2>
      ${records.slice(-5).reverse().map(r => `
        <div class="record-row">
          <div class="record-icon">${icon("wind")}</div>
          <div class="record-info">
            <div class="record-value">${r.acetone_ppm.toFixed(1)} ppm</div>
            <div class="record-time">${new Date(r.ts * 1000).toLocaleString("zh-CN")}</div>
          </div>
        </div>
      `).join("")}
    </section>

    <section class="today-logs">
      <h2>今日日志</h2>
      ${logs.length > 0 ? logs.map(log => `
        <div class="log-row">
          <div class="log-label">${log.kind === "meal" ? "饮食" : log.kind === "activity" ? "运动" : log.kind === "water" ? "饮水" : "体重"}</div>
          <div class="log-value">${log.value} ${log.unit || ""}</div>
        </div>
      `).join("") : "<p class='empty'>今日暂无日志</p>"}
    </section>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));

  el.querySelectorAll<HTMLElement>("[data-kind]").forEach(btn => {
    btn.addEventListener("click", async () => {
      const kind = btn.dataset.kind! as any;
      const value = kind === "meal" ? 1 : kind === "weight" ? 62.5 : kind === "water" ? 1 : 30;
      const unit = kind === "meal" ? "餐" : kind === "weight" ? "kg" : kind === "water" ? "杯" : "分钟";
      await api.addLog({ kind, value, unit, text: "" });
      void renderRecords(el);
    });
  });
}
