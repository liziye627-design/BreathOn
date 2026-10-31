import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";
import { mockReading } from "../lib/data";

export async function renderResult(el: HTMLElement) {
  const reading = mockReading();
  await api.save(reading, false);

  el.className = "page result";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>检测结果</h1>
    </header>

    <div class="result-hero">
      <img src="/ref/result_hero.png" alt="">
    </div>

    <div class="result-card">
      <div class="result-value">${reading.acetone_ppm.toFixed(1)}</div>
      <div class="result-unit">ppm</div>
      <div class="result-label">呼气丙酮水平</div>
      <div class="result-status good">正常范围</div>
    </div>

    <div class="result-note">
      <div class="note-icon">${icon("check_circle")}</div>
      <div>
        <strong>状态良好</strong>
        <p>继续保持健康的生活习惯</p>
      </div>
    </div>

    <div class="result-actions">
      <button class="secondary" data-go="trends">查看趋势</button>
      <button class="primary" data-go="home">完成</button>
    </div>

    <div class="result-disclaimer">
      以上数据仅供个人健康追踪参考，不构成医疗建议
    </div>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));
  el.querySelectorAll("[data-go]").forEach(btn => {
    btn.addEventListener("click", e => {
      const route = (e.currentTarget as HTMLElement).dataset.go!;
      go(route as any);
    });
  });
}
