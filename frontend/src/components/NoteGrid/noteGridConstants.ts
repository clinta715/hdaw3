import { NoteSnapshot } from "../../rpc/types";

export const KEY_HEIGHT = 8;
export const TOTAL_KEY_AREA = 128 * KEY_HEIGHT;
export const DRAG_THRESHOLD = 4;

export function clamp(val: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, val));
}

export const noteClipboardState: { items: NoteSnapshot[] } = { items: [] };
