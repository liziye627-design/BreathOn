import { invoke } from "@tauri-apps/api/core";
import { previewHandlers } from "./preview";

export type Source = "mock" | "sensor";
export type Quality = "ok" | "suspect" | "invalid" | "unknown";
export type Calibration = "uncalibrated" | "verified" | "not_applicable";
export type Range = "today" | "7d" | "30d" | "90d";
export type LogKind = "weight" | "meal" | "activity" | "water" | "medication_note" | "sleep";

export interface DeviceInfo {
  name: string; connected: boolean; battery: number; firmware: string;
  sensor_ready: boolean; transport: "mock" | "ble";
}
export interface Reading {
  acetone_ppm: number; source: Source; quality: Quality; calibration: Calibration; sensor_model: string | null;
}
export interface Measurement extends Reading { id: number; ts: number; consent_sync: boolean }
export interface UserLog { id?: number; ts?: number; kind: LogKind; value: number | null; unit: string | null; text: string }
export interface Consent { id: number; ts: number; scope: string; granted: boolean; measurement_id: number | null; version: string }
export interface Insight { is_llm: boolean; headline: string; summary: string; tips: [string, string][] }

/** 浏览器预览（非 Tauri）走内存/localStorage 回退，便于在 vite 中调界面。 */
export const inTauri = "__TAURI_INTERNALS__" in window;

function call<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  if (inTauri) return invoke<T>(cmd, args);
  const h = previewHandlers[cmd];
  if (!h) return Promise.reject(new Error(`预览模式不支持 ${cmd}`));
  try { return Promise.resolve(h(args) as T); } catch (e) { return Promise.reject(e); }
}

export const api = {
  deviceInfo: () => call<DeviceInfo>("device_info"),
  connect: () => call<DeviceInfo>("device_connect"),
  calibrate: () => call<void>("calibrate"),
  sample: () => call<Reading>("sample"),
  save: (m: Reading, sync: boolean, ts?: number) => call<number>("save_measurement", { m, sync, ts }),
  records: (range: Range) => call<Measurement[]>("get_records", { range }),
  addLog: (log: UserLog, ts?: number) => call<number>("add_log", { log, ts }),
  logsForDay: (dayTs: number) => call<{ logs: UserLog[] }>("logs_for_day", { dayTs }),
  deleteLog: (id: number) => call<number>("delete_log", { id }),
  setConsent: (scope: string, granted: boolean) => call<void>("set_consent", { scope, granted }),
  consentHistory: () => call<Consent[]>("consent_history"),
  getSetting: (key: string) => call<string | null>("get_setting", { key }),
  setSetting: (key: string, value: string) => call<void>("set_setting", { key, value }),
  exportRecords: () => call<string>("export_records"),
  deleteRecords: () => call<number>("delete_records"),
  insight: () => call<Insight>("insight"),
};
