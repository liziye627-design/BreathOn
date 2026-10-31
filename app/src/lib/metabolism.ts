/**
 * 演示模型：呼气丙酮 → 脂肪供能 / 热量估算。
 *
 * 目前没有传感器标定数据，下面的参数都是占位值，只用于把"气体 → 热量"的关系在界面上跑通。
 * 界面必须标注"估算·非实测"，不得作为医学结论。拿到标定数据后，只需替换 BANDS 与常量。
 */

export const BASELINE_PPM = 0.5;
/** 成人静息能耗约 1500–1700 kcal/天，取 65 kcal/h。 */
export const REE_KCAL_PER_HOUR = 65;
export const KCAL_PER_G_FAT = 9.4;
/** 两次检测间隔超过该值时，超出部分不计入全天估算。 */
export const MAX_GAP_HOURS = 4;

export interface Band {
  min: number; // ppm，含
  label: string;
  hint: string;
  fatShareAt: [number, number]; // 该区间下界、上界对应的脂肪供能占比
  tone: "low" | "mild" | "up" | "high";
}

/** 展示分级（非诊断）。最后一档上界用 5 ppm 做插值封顶。 */
export const BANDS: Band[] = [
  { min: 0, label: "以碳水供能为主", hint: "当前能量主要来自碳水", fatShareAt: [0.3, 0.35], tone: "low" },
  { min: 0.5, label: "脂肪供能轻度增加", hint: "身体开始更多动用脂肪", fatShareAt: [0.35, 0.5], tone: "mild" },
  { min: 1.0, label: "脂肪供能增加", hint: "脂肪供能占比较高", fatShareAt: [0.5, 0.65], tone: "up" },
  { min: 2.0, label: "脂肪供能明显", hint: "脂肪供能占比明显升高", fatShareAt: [0.65, 0.8], tone: "high" },
];
const TOP_PPM = 5;

export interface Estimate {
  ppm: number;
  band: Band;
  fatShare: number; // 0–1
  fatKcalPerHour: number;
  fatGramsPerHour: number;
}

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

export function bandOf(ppm: number): Band {
  let b = BANDS[0];
  for (const x of BANDS) if (ppm >= x.min) b = x;
  return b;
}

export function estimate(ppm: number): Estimate {
  const v = clamp(Number.isFinite(ppm) ? ppm : 0, 0, TOP_PPM);
  const i = BANDS.indexOf(bandOf(v));
  const band = BANDS[i];
  const hi = i + 1 < BANDS.length ? BANDS[i + 1].min : TOP_PPM;
  const t = clamp((v - band.min) / (hi - band.min), 0, 1);
  const fatShare = band.fatShareAt[0] + (band.fatShareAt[1] - band.fatShareAt[0]) * t;
  const fatKcalPerHour = REE_KCAL_PER_HOUR * fatShare;
  return { ppm: v, band, fatShare, fatKcalPerHour, fatGramsPerHour: fatKcalPerHour / KCAL_PER_G_FAT };
}

export interface Sample {
  ts: number; // 秒
  ppm: number;
}

/**
 * 全天估算：相邻两次检测之间按梯形累加每小时脂肪供能，间隔超过 MAX_GAP_HOURS 的部分不计。
 * 只有一次检测时，按该读数向后计 1 小时，避免显示 0。
 */
export function estimateRange(samples: Sample[]): { fatKcal: number; fatGrams: number; hours: number } {
  const s = [...samples].filter((x) => Number.isFinite(x.ppm)).sort((a, b) => a.ts - b.ts);
  if (s.length === 0) return { fatKcal: 0, fatGrams: 0, hours: 0 };
  if (s.length === 1) {
    const k = estimate(s[0].ppm).fatKcalPerHour;
    return { fatKcal: k, fatGrams: k / KCAL_PER_G_FAT, hours: 1 };
  }
  let kcal = 0;
  let hours = 0;
  for (let i = 1; i < s.length; i++) {
    const h = Math.min((s[i].ts - s[i - 1].ts) / 3600, MAX_GAP_HOURS);
    if (h <= 0) continue;
    kcal += ((estimate(s[i - 1].ppm).fatKcalPerHour + estimate(s[i].ppm).fatKcalPerHour) / 2) * h;
    hours += h;
  }
  return { fatKcal: kcal, fatGrams: kcal / KCAL_PER_G_FAT, hours };
}
