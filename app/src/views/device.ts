import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";

export async function renderDevice(el: HTMLElement) {
  const dev = await api.deviceInfo();
  const isConnected = dev.connected;

  el.className = "page device";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>连接设备</h1>
    </header>

    <div class="device-scene">
      <img src="/ref/connect_hero.png" alt="">
      <div class="status-indicator ${isConnected ? "on" : ""}"></div>
    </div>

    <div class="device-info">
      <h2>${isConnected ? "已连接" : "未连接"}</h2>
      <p>${isConnected ? `设备: ${dev.name}` : "请开启设备电源并靠近手机"}</p>
    </div>

    ${
      isConnected
        ? `<button class="primary" id="start-measure">${icon("wind")} 开始检测</button>`
        : `<button class="primary" id="scan">${icon("bluetooth")} 扫描设备</button>`
    }

    <div class="tip">
      <div class="tip-icon">${icon("alert")}</div>
      <div>确保设备已充电并在蓝牙范围内</div>
    </div>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));

  if (isConnected) {
    el.querySelector("#start-measure")!.addEventListener("click", () => go("measuring"));
  } else {
    el.querySelector("#scan")!.addEventListener("click", async () => {
      await api.connect();
      await renderDevice(el); // 重新渲染
    });
  }
}
