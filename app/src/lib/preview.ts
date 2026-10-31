// 浏览器预览用的内存后端，行为对齐 Rust 命令（校验规则、授权留痕、区间查询）。
import type { Consent, Measurement, Reading, UserLog } from "./api";

const KEY = "breathon.preview.v2";
type Store = { mem: Measurement[]; logs: UserLog[]; consents: Consent[]; settings: Record<string, string>; seq: number };
const load = (): Store => {
  try { return { mem: [], logs: [], consents: [], settings: {}, seq: 1, ...JSON.parse(localStorage.getItem(KEY) ?? "{}") }; }
  catch { return { mem: [], logs: [], consents: [], settings: {}, seq: 1 }; }
};
const st = load();
const save = () => localStorage.setItem(KEY, JSON.stringify(st));
const DAY = 86_400;
const nowS = () => Math.floor(Date.now() / 1000);
const dayStart = (ts: number) => { const d = new Date(ts * 1000); d.setHours(0, 0, 0, 0); return d.getTime() / 1000; };
const SPAN: Record<string, number> = { "7d": 7, "30d": 30, "90d": 90 };

const LOG_RANGE: Record<string, [number, number] | null> = {
  weight: [20, 300], activity: [1, 600], water: [1, 20], meal: [1, 20], sleep: [0.5, 24], medication_note: null,
};

function validateReading(m: Reading) {
  if (!Number.isFinite(m.acetone_ppm) || m.acetone_ppm < 0 || m.acetone_ppm > 300) throw "丙酮读数超出工程保护范围 0–300 ppm";
  if (m.source === "sensor" && !m.sensor_model) throw "传感器读数必须带 sensor_model";
  if (m.source === "mock" && m.calibration === "verified") throw "模拟读数不能标记为已校准";
}

function addConsent(scope: string, granted: boolean, mid: number | null) {
  st.consents.unshift({ id: st.seq++, ts: nowS(), scope, granted, measurement_id: mid, version: "1" });
}

export const previewHandlers: Record<string, (a: any) => unknown> = {
  device_info: () => ({ name: "灵息 BREATHON 智能挂坠", connected: true, battery: 86, firmware: "preview", sensor_ready: false, transport: "mock" }),
  device_connect: () => previewHandlers.device_info(0),
  calibrate: () => null,
  sample: (): Reading => ({ acetone_ppm: +(0.3 + Math.random() * 1.5).toFixed(2), source: "mock", quality: "unknown", calibration: "uncalibrated", sensor_model: null }),
  save_measurement: (a) => {
    validateReading(a.m);
    const id = st.seq++;
    st.mem.push({ ...a.m, id, ts: a.ts ?? nowS(), consent_sync: !!a.sync });
    addConsent("measurement_sync", !!a.sync, id);
    save();
    return id;
  },
  get_records: (a) => {
    const from = a.range === "today" ? dayStart(nowS()) : nowS() - (SPAN[a.range] ?? 30) * DAY;
    return st.mem.filter((m) => m.ts >= from).sort((x, y) => x.ts - y.ts);
  },
  add_log: (a) => {
    const l: UserLog = a.log;
    const r = LOG_RANGE[l.kind];
    if (r === undefined) throw "不支持的记录类型";
    if (r === null ? !l.text.trim() : l.value == null || l.value < r[0] || l.value > r[1]) throw "记录数值缺失或超出合理范围";
    const id = st.seq++;
    st.logs.push({ ...l, id, ts: a.ts ?? nowS() });
    save();
    return id;
  },
  logs_for_day: (a) => {
    const s = dayStart(a.dayTs);
    return { logs: st.logs.filter((l) => l.ts! >= s && l.ts! < s + DAY) };
  },
  delete_log: (a) => { const n = st.logs.length; st.logs = st.logs.filter((l) => l.id !== a.id); save(); return n - st.logs.length; },
  set_consent: (a) => { st.settings.consent_upload_default = a.granted ? "1" : "0"; addConsent(a.scope, a.granted, null); save(); return null; },
  consent_history: () => st.consents.slice(0, 50),
  get_setting: (a) => st.settings[a.key] ?? null,
  set_setting: (a) => { st.settings[a.key] = a.value; save(); return null; },
  export_records: () => JSON.stringify({ measurements: st.mem, user_logs: st.logs, consents: st.consents }, null, 2),
  delete_records: () => {
    const n = st.mem.length;
    st.mem = []; st.logs = []; st.consents = st.consents.filter((c) => c.measurement_id == null);
    save();
    return n;
  },
  insight: () => {
    const rows = st.mem.filter((m) => m.ts >= nowS() - 7 * DAY);
    const mock = rows.some((m) => m.source === "mock");
    return {
      is_llm: false,
      headline: !rows.length ? "还没有检测记录" : mock ? "这里包含工程测试输入" : "已记录传感器输入",
      summary: !rows.length ? "完成几次检测后，这里会汇总你的长期趋势。" : `最近 7 日有 ${rows.length} 条记录${mock ? "，其中含模拟数据，仅用于验证流程，不代表真实呼气结果" : ""}。`,
      tips: [["规律饮食", "保持均衡营养，避免过度节食"], ["适量活动", "按自己的节奏安排日常活动"], ["充足饮水", "把饮水量记在“记录”里，观察自己的习惯"]],
    };
  },
};
