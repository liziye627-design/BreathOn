import { api, Reading } from "./api";

/**
 * 演示数据：首次在模拟设备上启动时，生成近 14 天的随机记录，让各页面有内容可看。
 *
 * 取值范围对齐设计稿里出现的数值（丙酮 0.3–1.8 ppm，体重 60 kg 左右）。
 * 这只是演示范围，不是医学参考区间；所有读数都以 source=mock 写入，界面会带「非实测」标签。
 * 不生成任何具体药品名称的用药记录。
 */
const DAY = 86_400;
const rnd = (lo: number, hi: number, digits = 1) => Number((lo + Math.random() * (hi - lo)).toFixed(digits));
const int = (lo: number, hi: number) => Math.floor(lo + Math.random() * (hi - lo + 1));

export const mockReading = (): Reading => ({
  acetone_ppm: rnd(0.3, 1.8, 2),
  source: "mock",
  quality: "unknown",
  calibration: "uncalibrated",
  sensor_model: null,
});

export async function seedDemoData(days = 14): Promise<number> {
  const now = Math.floor(Date.now() / 1000);
  let weight = rnd(60, 64);
  let count = 0;

  for (let d = days; d >= 0; d--) {
    const dayStart = now - d * DAY;
    const tests = d === 0 ? 1 : Math.random() < 0.4 ? 2 : 1;

    for (let i = 0; i < tests; i++) {
      // 今天的检测放在 10 分钟到 3 小时前，保证时间戳不晚于当前
      const ts = d === 0 ? now - int(600, 3 * 3600) : dayStart - int(0, 10 * 3600);
      await api.save(mockReading(), false, ts);
      count++;
    }

    const base = d === 0 ? now - 3600 : dayStart - 4 * 3600;
    weight = Math.min(66, Math.max(58, weight + rnd(-0.4, 0.3)));
    await api.addLog({ kind: "weight", value: Number(weight.toFixed(1)), unit: "kg", text: "" }, base);
    for (let m = 0; m < int(2, 4); m++) await api.addLog({ kind: "meal", value: 1, unit: "餐", text: "" }, base + m);
    await api.addLog({ kind: "activity", value: int(20, 45), unit: "分钟", text: "" }, base + 10);
    await api.addLog({ kind: "water", value: int(4, 8), unit: "杯", text: "" }, base + 20);
  }
  await api.setSetting("demo_seeded", "1");
  return count;
}

/** 仅在模拟设备、从未生成过演示数据、且库为空时生成。 */
export async function seedIfFirstRun(): Promise<boolean> {
  const [dev, seeded, rows] = await Promise.all([api.deviceInfo(), api.getSetting("demo_seeded"), api.records("90d")]);
  if (dev.transport !== "mock" || seeded === "1" || rows.length > 0) return false;
  await seedDemoData();
  return true;
}
