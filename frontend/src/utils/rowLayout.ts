// frontend/src/utils/rowLayout.ts
//
// Shared vertical-layout model for the arrange view. The track-header column
// and the timeline lanes MUST agree on where each track row sits, otherwise
// they drift out of register (the historical "headers don't line up with the
// lanes" bug). Both derive their geometry from a single RowLayout built from
// the tracks' persisted heights, so rows always align and the per-track
// resize handle is honoured everywhere — not just written to the model.

export const DEFAULT_TRACK_HEIGHT = 56;

export interface RowLayout {
  /** tops[i] = top offset (px, content space) of visible row i. */
  tops: number[];
  /** heights[i] = height (px) of visible row i. */
  heights: number[];
  /** Sum of all row heights — total scrollable content height. */
  total: number;
  /** Number of visible rows. */
  count: number;
}

function sanitizeHeight(h: unknown): number {
  return typeof h === "number" && Number.isFinite(h) && h > 0
    ? h
    : DEFAULT_TRACK_HEIGHT;
}

export function buildRowLayout(rawHeights: ArrayLike<unknown>): RowLayout {
  const count = rawHeights.length;
  const tops = new Array<number>(count);
  const heights = new Array<number>(count);
  let acc = 0;
  for (let i = 0; i < count; i++) {
    const h = sanitizeHeight(rawHeights[i]);
    tops[i] = acc;
    heights[i] = h;
    acc += h;
  }
  return { tops, heights, total: acc, count };
}

/**
 * Index of the row containing `y` (content-space). Returns `count` when `y`
 * is past the bottom of the last row — mirroring the legacy
 * `Math.floor(y / TRACK_HEIGHT)`, which could yield one-past-the-end for a
 * drop below the last track (used to trigger "create a new track").
 */
export function rowAtYOrCount(layout: RowLayout, y: number): number {
  const { tops, total, count } = layout;
  if (count === 0) return 0;
  if (y >= total) return count;
  if (y < 0) return 0;
  // Binary search for the largest i with tops[i] <= y — the containing row,
  // since rows are contiguous (tops[i+1] === tops[i] + heights[i]).
  let lo = 0;
  let hi = count - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (tops[mid] <= y) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

/** Index of the row containing `y`, clamped to the last row. */
export function rowAtY(layout: RowLayout, y: number): number {
  const i = rowAtYOrCount(layout, y);
  return i >= layout.count ? Math.max(0, layout.count - 1) : i;
}
