import { describe, it, expect } from "vitest";
import {
  DEFAULT_TRACK_HEIGHT,
  buildRowLayout,
  rowAtY,
  rowAtYOrCount,
} from "./rowLayout";
import { computeRubberBandSelection } from "./timelineConstants";

describe("buildRowLayout", () => {
  it("builds prefix-sum tops for uniform heights", () => {
    const layout = buildRowLayout([56, 56, 56]);
    expect(layout.count).toBe(3);
    expect(layout.tops).toEqual([0, 56, 112]);
    expect(layout.heights).toEqual([56, 56, 56]);
    expect(layout.total).toBe(168);
  });

  it("builds prefix-sum tops for variable heights", () => {
    const layout = buildRowLayout([40, 80, 60]);
    expect(layout.tops).toEqual([0, 40, 120]);
    expect(layout.heights).toEqual([40, 80, 60]);
    expect(layout.total).toBe(180);
  });

  it("falls back to the default for missing/invalid heights", () => {
    const layout = buildRowLayout([undefined, 0, -5, NaN, 100]);
    expect(layout.heights).toEqual([
      DEFAULT_TRACK_HEIGHT,
      DEFAULT_TRACK_HEIGHT,
      DEFAULT_TRACK_HEIGHT,
      DEFAULT_TRACK_HEIGHT,
      100,
    ]);
    expect(layout.tops[4]).toBe(DEFAULT_TRACK_HEIGHT * 4);
  });

  it("handles an empty track list", () => {
    const layout = buildRowLayout([]);
    expect(layout.count).toBe(0);
    expect(layout.total).toBe(0);
  });
});

describe("rowAtY / rowAtYOrCount", () => {
  const layout = buildRowLayout([40, 80, 60]); // tops [0,40,120], total 180

  it("returns the containing row", () => {
    expect(rowAtY(layout, 0)).toBe(0);
    expect(rowAtY(layout, 39)).toBe(0);
    expect(rowAtY(layout, 40)).toBe(1);
    expect(rowAtY(layout, 100)).toBe(1);
    expect(rowAtY(layout, 120)).toBe(2);
    expect(rowAtY(layout, 179)).toBe(2);
  });

  it("clamps negative y to the first row", () => {
    expect(rowAtY(layout, -10)).toBe(0);
  });

  it("clamps past-the-bottom to the last row", () => {
    expect(rowAtY(layout, 500)).toBe(2);
  });

  it("rowAtYOrCount returns count when below the last row (drop-to-create)", () => {
    expect(rowAtYOrCount(layout, 180)).toBe(3);
    expect(rowAtYOrCount(layout, 999)).toBe(3);
    expect(rowAtYOrCount(layout, 179)).toBe(2);
  });

  it("matches legacy Math.floor(y / h) for uniform heights", () => {
    const uniform = buildRowLayout([56, 56, 56]);
    for (const y of [0, 10, 55, 56, 57, 111, 112, 167]) {
      expect(rowAtYOrCount(uniform, y)).toBe(Math.floor(y / 56));
    }
    // Below the last row both yield one-past-the-end.
    expect(rowAtYOrCount(uniform, 200)).toBe(Math.floor(200 / 56));
  });
});

describe("computeRubberBandSelection (layout-aware)", () => {
  const clips = [
    { clipId: 1, trackIndex: 0, startBeat: 0, durationBeats: 4 },
    { clipId: 2, trackIndex: 1, startBeat: 4, durationBeats: 4 },
    { clipId: 3, trackIndex: 2, startBeat: 8, durationBeats: 4 },
  ];

  it("selects clips whose track rows fall inside the band", () => {
    const layout = buildRowLayout([56, 56, 56]);
    // Band covering rows 0-1 across beats 0-8 at pps=10.
    const sel = computeRubberBandSelection(0, 0, 80, 111, clips, 10, layout);
    expect(sel.has(1)).toBe(true);
    expect(sel.has(2)).toBe(true);
    expect(sel.has(3)).toBe(false);
  });

  it("a bottom edge exactly on a row boundary does not select that row", () => {
    const layout = buildRowLayout([56, 56, 56]);
    // Bottom edge exactly at y=56 (top of row 1) → only row 0 selected.
    const sel = computeRubberBandSelection(0, 0, 200, 56, clips, 10, layout);
    expect(sel.has(1)).toBe(true);
    expect(sel.has(2)).toBe(false);
  });

  it("respects variable row heights", () => {
    const layout = buildRowLayout([40, 80, 60]); // tops [0,40,120]
    // Band from y=50..100 lies entirely within row 1 (40..120).
    const sel = computeRubberBandSelection(0, 50, 200, 100, clips, 10, layout);
    expect(sel.has(2)).toBe(true);
    expect(sel.has(1)).toBe(false);
    expect(sel.has(3)).toBe(false);
  });

  it("returns empty for an empty layout", () => {
    const sel = computeRubberBandSelection(0, 0, 100, 100, clips, 10, buildRowLayout([]));
    expect(sel.size).toBe(0);
  });
});
