import "./splash.css";

/** 启动屏保：全屏插画，约 2.6 秒后或轻触时淡出。首页在下面同时渲染，淡出后直接可用。 */
export function showSplash(ms = 2600) {
  const el = document.createElement("div");
  el.className = "splash";
  el.tabIndex = 0;
  el.setAttribute("role", "button");
  el.setAttribute("aria-label", "进入灵息");
  el.innerHTML = `
    <div class="splash-art" aria-hidden="true"></div>
    <div class="splash-brand">
      <img src="/ref/logo.png" alt="">
      <div class="zh">灵息</div>
      <div class="en">Breath On</div>
      <p>让呼吸，变成更轻盈的开始</p>
    </div>
    <div class="splash-hint">轻触进入</div>`;
  document.body.appendChild(el);
  // 屏保期间锁住底层页面滚动，避免滚动条透出
  const root = document.documentElement;
  const prevOverflow = root.style.overflow;
  root.style.overflow = "hidden";

  let done = false;
  const close = () => {
    if (done) return;
    done = true;
    clearTimeout(timer);
    el.classList.add("out");
    setTimeout(() => {
      el.remove();
      root.style.overflow = prevOverflow;
    }, 450);
  };
  const timer = setTimeout(close, ms);
  el.addEventListener("click", close);
  el.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") close();
  });
}
