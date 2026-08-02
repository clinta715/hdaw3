import { describe, it, expect } from "vitest";
import {
  divisionToBeats,
  snapToGrid,
  snapWithOffset,
  snapToEvents,
  snap,
  collectClipEdges,
} from "./snapUtils";

describe("divisionToBeats / snapToGrid (backward compat)", () => {
  it("maps division indices to beat sizes", () => {
    expect(divisionToBeats(0)).toBe(4);
    expect(divisionToBeats(1)).toBe(1);
    expect(divisionToBeats(2)).toBe(0.5);
    expect(divisionToBeats(3)).toBe(0.25);
    expect(divisionToBeats(4)).toBe(0.125);
    expect(divisionToBeats(99)).toBe(1);
  });

  it("snaps to the nearest grid line", () => {
    expect(snapToGrid(0.4, 1)).toBe(0);
    expect(snapToGrid(0.6, 1)).toBe(1);
    expect(snapToGrid(1.24, 3)).toBe(1.25);
    expect(snapToGrid(3.9, 0)).toBe(4);
  });
});

describe("snapWithOffset", () => {
  it("preserves the sub-grid offset of the original start", () => {
    // original start at 0.1 (off-grid); moving to ~2.2 snaps the DELTA (2.1)
    // to the grid (2) and re-adds the offset → 2.1, not 2.0.
    expect(snapWithOffset(2.2, 0.1, 1)).toBeCloseTo(2.1, 10);
    expect(snapWithOffset(0.1, 0.1, 1)).toBeCloseTo(0.1, 10);
    expect(snapWithOffset(1.7, 0.25, 1)).toBeCloseTo(1.25, 10);
  });
});

describe("snapToEvents", () => {
  const edges = [1, 2, 4];
  it("picks the nearest edge within threshold", () => {
    expect(snapToEvents(2.04, edges, 0.06)).toBe(2);
    expect(snapToEvents(0.97, edges, 0.06)).toBe(1);
    expect(snapToEvents(4.05, edges, 0.06)).toBe(4);
  });
  it("returns null when nothing is within threshold", () => {
    expect(snapToEvents(2.5, edges, 0.06)).toBeNull();
    expect(snapToEvents(0, [], 0.06)).toBeNull();
  });
  it("picks the closer of two equidistant-ish edges", () => {
    expect(snapToEvents(1.51, [1, 2], 0.6)).toBe(2);
    expect(snapToEvents(1.49, [1, 2], 0.6)).toBe(1);
  });
});

describe("snap facade", () => {
  it("disabled returns the input unchanged", () => {
    expect(snap(1.234, { enabled: false, division: 1 })).toBe(1.234);
    expect(snap(0.777, { enabled: false, division: 3 }, { originalStart: 0.1, edges: [1] })).toBe(0.777);
  });

  it("with defaults equals snapToGrid exactly (backward compat)", () => {
    const values = [0, 0.4, 0.6, 1.24, 2.5, 3.9, 7.1];
    for (const div of [0, 1, 2, 3, 4]) {
      for (const v of values) {
        expect(snap(v, { enabled: true, division: div })).toBe(snapToGrid(v, div));
      }
    }
  });

  it("gridOffset preserves the existing offset", () => {
    const res = snap(2.2, { enabled: true, division: 1, gridOffset: true }, { originalStart: 0.1 });
    expect(res).toBeCloseTo(2.1, 10);
  });

  it("gridOffset no-ops when originalStart is absent (falls back to grid)", () => {
    expect(snap(2.2, { enabled: true, division: 1, gridOffset: true })).toBe(snapToGrid(2.2, 1));
  });

  it("grid + events picks the closest anchor", () => {
    // grid (div 1) wants 2.0; an event edge at 2.04 is closer to 2.03 → event wins.
    const edgeWin = snap(2.03, { enabled: true, division: 1, events: true }, { edges: [2.04] });
    expect(edgeWin).toBe(2.04);
    // grid line 2.0 is closer to 2.0 than an edge at 2.2 → grid wins.
    const gridWin = snap(2.02, { enabled: true, division: 1, events: true }, { edges: [2.2] });
    expect(gridWin).toBe(2);
  });

  it("events no-ops when no edge is within threshold (falls back to grid)", () => {
    const res = snap(2.4, { enabled: true, division: 1, events: true, eventThresholdBeats: 0.06 }, { edges: [5] });
    expect(res).toBe(2);
  });
});

describe("collectClipEdges", () => {
  const clips = [
    { clipId: 1, startBeat: 2, durationBeats: 1 },
    { clipId: 2, startBeat: 0, durationBeats: 0.5 },
    { clipId: 3, startBeat: 5, durationBeats: 2 },
  ];

  it("returns sorted start + end edges", () => {
    expect(collectClipEdges(clips)).toEqual([0, 0.5, 2, 3, 5, 7]);
  });

  it("excludes the dragged clip id", () => {
    expect(collectClipEdges(clips, new Set([1]))).toEqual([0, 0.5, 5, 7]);
  });

  it("uses a custom id accessor", () => {
    const custom = [{ id: 9, startBeat: 1, durationBeats: 1 }];
    expect(collectClipEdges(custom, new Set([9]), (c: any) => c.id)).toEqual([]);
    expect(collectClipEdges(custom, new Set([5]), (c: any) => c.id)).toEqual([1, 2]);
  });
});
