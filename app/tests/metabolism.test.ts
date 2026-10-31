// 运行：node --experimental-strip-types --test tests/metabolism.test.ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { bandOf, estimate, estimateRange, REE_KCAL_PER_HOUR, MAX_GAP_HOURS } from "../src/lib/metabolism.ts";

test("分级边界", () => {
  assert.equal(bandOf(0.49).tone, "low");
  assert.equal(bandOf(0.5).tone, "mild");
  assert.equal(bandOf(1.0).tone, "up");
  assert.equal(bandOf(2.0).tone, "high");
});

test("脂肪供能占比随丙酮单调不减，且在 30%–80% 之间", () => {
  let last = 0;
  for (let p = 0; p <= 6; p += 0.05) {
    const s = estimate(p).fatShare;
    assert.ok(s >= last - 1e-9, `ppm ${p.toFixed(2)} 占比下降`);
    assert.ok(s >= 0.3 && s <= 0.8);
    last = s;
  }
});

test("区间交界处连续", () => {
  for (const b of [0.5, 1.0, 2.0]) {
    assert.ok(Math.abs(estimate(b - 1e-6).fatShare - estimate(b).fatShare) < 1e-4);
  }
});

test("异常输入不崩溃", () => {
  assert.equal(estimate(NaN).ppm, 0);
  assert.equal(estimate(-3).ppm, 0);
  assert.equal(estimateRange([]).fatKcal, 0);
});

test("全天累加：梯形积分 + 长间隔截断", () => {
  const k = estimate(1.0).fatKcalPerHour;
  const two = estimateRange([{ ts: 0, ppm: 1.0 }, { ts: 2 * 3600, ppm: 1.0 }]);
  assert.ok(Math.abs(two.fatKcal - 2 * k) < 1e-6);
  const gap = estimateRange([{ ts: 0, ppm: 1.0 }, { ts: 10 * 3600, ppm: 1.0 }]);
  assert.equal(gap.hours, MAX_GAP_HOURS);
  const one = estimateRange([{ ts: 0, ppm: 0.2 }]);
  assert.ok(one.fatKcal > 0 && one.fatKcal < REE_KCAL_PER_HOUR);
});
