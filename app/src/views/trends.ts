import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";

export async function renderTrends(el: HTMLElement) {
  const records = await api.records("30d");
  const recent7 = records.slice(-7); // 升序返回，取最后 7 条

  const values = recent7.map(r => r.acetone_ppm);
  const max = Math.max(...values, 2.0);
  const points = values.map((v, i) => {
    const x = (i / (values.length - 1)) * 100;
    const y = 100 - (v / max) * 80;
    return `${x},${y}`;
  }).join(" ");

  el.className = "page trends";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>趋势分析</h1>
    </header>

    <div class="trends-hero">
      <img src="/ref/trends_hero.png" alt="">
    </div>

    <section class="chart-section">
      <h2>近7天呼气丙酮</h2>
      <svg viewBox="0 0 100 100" class="trend-chart">
        <polyline points="${points}" fill="none" stroke="var(--primary)" stroke-width="2" />
        ${values.map((v, i) => `<circle cx="${(i / (values.length - 1)) * 100}" cy="${100 - (v / max) * 80}" r="3" fill="var(--primary)" />`).join("")}
      </svg>
      <div class="chart-labels">
        ${values.map(v => `<div>${v.toFixed(1)}</div>`).join("")}
      </div>
    </section>

    <section class="stats-grid">
      <div class="stat-box">
        <div class="stat-label">平均值</div>
        <div class="stat-value">${(values.reduce((a, b) => a + b, 0) / values.length).toFixed(1)} ppm</div>
      </div>
      <div class="stat-box">
        <div class="stat-label">最高值</div>
        <div class="stat-value">${Math.max(...values).toFixed(1)} ppm</div>
      </div>
    </section>

    <div class="note">
      ${icon("check_circle")} 趋势平稳，继续保持
    </div>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));
}
