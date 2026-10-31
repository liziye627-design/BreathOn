import { api } from "../lib/api";
import { icon } from "../lib/icons";
import { go } from "../main";

const QUESTIONS = [
  "我该如何改善我的健康状况？",
  "最近的检测数据说明了什么？",
  "有什么饮食建议吗？",
];

export async function renderChat(el: HTMLElement) {
  const insight = await api.insight();

  el.className = "page chat";
  el.innerHTML = `
    <header class="page-header">
      <button class="back-btn" aria-label="返回">${icon("back")}</button>
      <h1>AI健康伙伴</h1>
    </header>

    <div class="chat-hero">
      <img src="/ref/chat_hero.png" alt="">
    </div>

    <div class="chat-intro">
      <h2>你好！我是你的健康伙伴</h2>
      <p>基于你的健康数据，我可以提供个性化建议</p>
    </div>

    <div class="message-list">
      <div class="msg assistant">
        <div class="msg-avatar">${icon("bot")}</div>
        <div class="msg-content">
          <strong>${insight.headline}</strong>
          <p>${insight.summary}</p>
        </div>
      </div>
    </div>

    <div class="quick-questions">
      <h3>你可以问我：</h3>
      ${QUESTIONS.map(q => `<button class="question-btn">${q}</button>`).join("")}
    </div>

    <div class="chat-disclaimer">
      ${icon("alert")} AI建议仅供参考，不构成医疗诊断或治疗建议
    </div>`;

  el.querySelector(".back-btn")!.addEventListener("click", () => go("home"));

  el.querySelectorAll(".question-btn").forEach(btn => {
    btn.addEventListener("click", async () => {
      const q = btn.textContent!;
      const msgList = el.querySelector(".message-list")!;
      msgList.innerHTML += `
        <div class="msg user">
          <div class="msg-content">${q}</div>
        </div>
        <div class="msg assistant">
          <div class="msg-avatar">${icon("bot")}</div>
          <div class="msg-content">感谢你的提问！根据你的数据，建议继续保持良好的生活习惯。</div>
        </div>`;
      msgList.scrollTop = msgList.scrollHeight;
    });
  });
}
