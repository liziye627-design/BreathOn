import type { Reading } from "./api";

/** 单次检测的页面间状态：检测页写入，结果页读取。仅存内存，不持久化。 */
export const session: { reading: Reading | null; measuredAt: number | null; savedId: number | null } = {
  reading: null,
  measuredAt: null,
  savedId: null,
};

export function resetSession() {
  session.reading = null;
  session.measuredAt = null;
  session.savedId = null;
}
