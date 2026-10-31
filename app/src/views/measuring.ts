import { icon } from "../lib/icons";
import { go } from "../main";

let abortCtrl: AbortController | null = null;

export async function renderMeasuring(el: HTMLElement) {
  el.className = "page measuring";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="取消">${icon("back")}</button>
      <h1>正在检测</h1>
    </header>

    <div class="measuring-scene">
      <img src="/ref/measuring_scene.png" alt="" class="pulse">
      <div class="countdown" id="countdown">00:18</div>
    </div>

    <div class="instruction">
      <h2>请深呼吸</h2>
      <p>对准设备呼气，保持平稳</p>
    </div>

    <div class="progress-bar"><div class="fill" id="progress"></div></div>`;

  const countdownEl = el.querySelector<HTMLElement>("#countdown")!;
  const progressEl = el.querySelector<HTMLElement>("#progress")!;
  let elapsed = 0;
  const duration = 18;

  abortCtrl = new AbortController();
  const timer = setInterval(
    () => {
      if (abortCtrl?.signal.aborted) {
        clearInterval(timer);
        return;
      }
      elapsed++;
      const remaining = duration - elapsed;
      countdownEl.textContent = `00:${remaining.toString().padStart(2, "0")}`;
      progressEl.style.width = `${(elapsed / duration) * 100}%`;

      if (elapsed >= duration) {
        clearInterval(timer);
        void go("result");
      }
    },
    1000
  );

  el.querySelector(".back-btn")!.addEventListener("click", () => {
    abortCtrl?.abort();
    go("home");
  });
}
