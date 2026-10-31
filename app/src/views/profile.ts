import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";

export async function renderProfile(el: HTMLElement) {
  const records = await api.records("90d");
  const totalTests = records.length;

  el.className = "page profile";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>个人中心</h1>
    </header>

    <div class="profile-hero">
      <img src="/ref/profile_hero.png" alt="">
    </div>

    <div class="profile-card">
      <img src="/ref/avatar.png" alt="头像" class="avatar">
      <div class="profile-info">
        <h2>健康追踪者</h2>
        <p>已使用 ${totalTests} 天</p>
      </div>
    </div>

    <section class="profile-stats">
      <div class="stat-item">
        <div class="stat-num">${totalTests}</div>
        <div class="stat-label">累计检测</div>
      </div>
      <div class="stat-item">
        <div class="stat-num">${Math.floor(totalTests / 7)}</div>
        <div class="stat-label">连续周数</div>
      </div>
    </section>

    <section class="menu-list">
      <button class="menu-item">${icon("calendar")} 数据同步</button>
      <button class="menu-item">${icon("lock")} 隐私设置</button>
      <button class="menu-item">${icon("settings")} 偏好设置</button>
      <button class="menu-item" id="export">${icon("upload")} 导出数据</button>
    </section>

    <div class="app-info">
      <p>灵息 BREATHON v1.0.0</p>
      <p class="legal">个人健康追踪工具，不提供医疗诊断服务</p>
    </div>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));
  el.querySelector("#export")!.addEventListener("click", async () => {
    const csv = await api.exportRecords();
    const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `breathon_records_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  });
}
