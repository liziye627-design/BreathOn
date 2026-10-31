import { api, UserLog } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";
import { estimate, estimateRange } from "../lib/metabolism";
import "./home.css";

export async function renderHome(el: HTMLElement) {
  const [records, todayResult] = await Promise.all([
    api.records("7d"),
    api.logsForDay(Math.floor(Date.now() / 1000))
  ]);

  // records 按时间升序返回：最后一条才是最新
  const latest = records[records.length - 1];
  const ketone = latest?.acetone_ppm ?? 0.8;
  const prev = records[records.length - 2]?.acetone_ppm ?? ketone;
  const delta = ketone - prev;

  // 气体 → 热量（演示模型，界面标注"估算·非实测"）
  const est = estimate(ketone);
  const dayStart = new Date().setHours(0, 0, 0, 0) / 1000;
  const today = estimateRange(records.filter((r) => r.ts >= dayStart).map((r) => ({ ts: r.ts, ppm: r.acetone_ppm })));

  const logs = todayResult.logs;
  const weight = logs.find((l: UserLog) => l.kind === "weight")?.value ?? 62.3;
  const meals = logs.filter((l: UserLog) => l.kind === "meal").length;
  const mealsTarget = 4;
  const activity = logs.find((l: UserLog) => l.kind === "activity")?.value ?? 30;
  const activityTarget = 40;
  const water = logs.find((l: UserLog) => l.kind === "water")?.value ?? 5;
  const waterTarget = 8;
  const dailyPct = Math.round(((meals / mealsTarget) * 0.3 + (activity / activityTarget) * 0.4 + (water / waterTarget) * 0.3) * 100);

  el.className = "page home";
  el.innerHTML = `
    <div class="brand">
      <img src="/ref/logo.png" alt="">
      <div class="brand-zh">灵息</div>
      <div class="brand-en">Breath On</div>
    </div>
    <button class="bell" aria-label="通知">${icon("bell")}</button>
    <div class="greeting">早上好，<br>继续保持专注！</div>
    <div class="subtext">每一次呼吸，都是更健康的你。</div>
    <img src="/ref/home_hero.png" alt="" class="hero-scene">
    <div class="kpi-card">
      <img src="/ref/home_card_deco.png" alt="" class="kpi-deco">
      <div class="kpi-label">当前丙酮水平 <span class="kpi-info">i</span></div>
      <div class="kpi-value">${ketone.toFixed(1)}<span class="kpi-unit">ppm</span></div>
      <div class="kpi-pill tone-${est.band.tone}">${icon("wave")} ${est.band.label}</div>
      <div class="kpi-kcal">
        <div class="kcal-cap">今日估算脂肪供能</div>
        ${today.hours > 0
          ? `<div class="kcal-val">${Math.round(today.fatKcal)}<small>kcal</small></div>
        <div class="kcal-sub">约 ${today.fatGrams.toFixed(1)} g 脂肪 · 当前 ${Math.round(est.fatShare * 100)}%</div>`
          : `<div class="kcal-val">—</div>
        <div class="kcal-sub">今天还没有检测</div>`}
        <div class="kcal-tag">估算·非实测</div>
      </div>
      ${delta !== 0 ? `<div class="kpi-compare">${icon("arrow_right")} 较上次 ${delta > 0 ? "↑" : "↓"} ${Math.abs(delta).toFixed(1)}</div>` : ""}
      <button class="kpi-btn" id="startTest">${icon("wind")} 开始检测 <span class="ic arrow">${icon("arrow_right")}</span></button>
    </div>
    <div class="progress-card">
      <div class="progress-title">今日进度</div>
      <div class="progress-tip">小小的坚持，带来大大的改变。${icon("chevron_right")}</div>
      <div class="day-ring">
        <svg viewBox="0 0 116 116">
          <defs>
            <linearGradient id="gr1" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0" stop-color="#b8a4fe"/>
              <stop offset="1" stop-color="#b8a4fe"/>
            </linearGradient>
          </defs>
          <circle cx="58" cy="58" r="50" fill="none" stroke="#f6f0e4" stroke-width="8"/>
          <circle cx="58" cy="58" r="50" fill="none" stroke="url(#gr1)" stroke-width="8.5"
            stroke-dasharray="${(dailyPct / 100) * 314} 314" stroke-linecap="round"
            transform="rotate(-90 58 58)"/>
        </svg>
        <div class="ring-pct">${dailyPct}<small>%</small></div>
        <div class="ring-cap">今日目标</div>
      </div>
      <div class="mini meal"><span class="ic">${icon("utensils")}</span> <div><div class="lab">饮食</div><div class="val">${meals}<small>/${mealsTarget}餐</small></div></div></div>
      <div class="mini act"><span class="ic">${icon("run")}</span> <div><div class="lab">运动</div><div class="val">${activity}<small>/${activityTarget}分钟</small></div></div></div>
      <div class="mini wt"><span class="ic">${icon("weight")}</span> <div><div class="lab">体重</div><div class="val">${weight.toFixed(1)} <small>kg</small></div></div></div>
      <div class="mini wtr"><span class="ic">${icon("droplet")}</span> <div><div class="lab">饮水</div><div class="val">${water}<small>/${waterTarget}杯</small></div></div></div>
    </div>
    <img src="/ref/home_mascot.png" alt="" class="tip-mascot">
    <div class="tip-bubble">
      <div class="tip-title">${est.band.label}</div>
      <div class="tip-text">${est.band.hint}，当前约 ${est.fatKcalPerHour.toFixed(0)} kcal/小时来自脂肪（估算）。</div>
    </div>`;

  el.querySelector("#startTest")!.addEventListener("click", () => go("device"));
}
