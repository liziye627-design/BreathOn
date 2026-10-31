import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { seedIfFirstRun } from "../lib/data";
import { go } from "../main";

/** 欢迎页：版式、文案按参考图 01_onboarding 还原。 */
export function renderWelcome(el: HTMLElement) {
  el.className = "page welcome";
  el.innerHTML = `
    <div class="brand-top brand-row">
      <img src="/ref/logo.png" alt="">
      <div><div class="zh">灵息</div><div class="en">Breath On</div></div>
    </div>
    <div class="scene"><img src="/ref/onboard_scene.png" alt="灵息挂坠"></div>
    <div class="copy">
      <h1>让呼吸，<br>变成更轻盈的开始</h1>
      <p>通过呼吸，了解你身体的代谢状态，<br>开启更健康、更自在的生活。</p>
    </div>
    <button class="cta" id="start">开始体验${icon("arrow_right")}</button>
    <div class="dots" aria-hidden="true"><i class="on"></i><i></i><i></i></div>`;

  el.querySelector<HTMLElement>("#start")!.onclick = async () => {
    await api.setSetting("onboarded", "1");
    await seedIfFirstRun();
    void go("home");
  };
}
