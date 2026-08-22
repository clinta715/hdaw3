import { describe, it, expect, beforeEach } from "vitest";
import { useMeterStore } from "../store/meterStore";

describe("meterStore", () => {
  beforeEach(() => {
    useMeterStore.setState({
      master: { l: 0, r: 0, rmsL: 0, rmsR: 0, lufs: -70 },
      tracks: [],
    });
  });

  it("starts with zero meters", () => {
    const { master, tracks } = useMeterStore.getState();
    expect(master.l).toBe(0);
    expect(master.r).toBe(0);
    expect(tracks).toEqual([]);
  });

  it("updates master and track meters", () => {
    useMeterStore.getState().update({
      master: { l: 0.5, r: 0.6, rmsL: 0.4, rmsR: 0.5, lufs: -12 },
      tracks: [
        { l: 0.1, r: 0.2, rmsL: 0.08, rmsR: 0.15, lufs: -20 },
        { l: 0.3, r: 0.4, rmsL: 0.25, rmsR: 0.35, lufs: -15 },
      ],
    });

    const { master, tracks } = useMeterStore.getState();
    expect(master.l).toBe(0.5);
    expect(master.r).toBe(0.6);
    expect(tracks).toHaveLength(2);
    expect(tracks[0].l).toBe(0.1);
    expect(tracks[1].r).toBe(0.4);
  });

  it("replaces previous meter state", () => {
    useMeterStore.getState().update({
      master: { l: 0.9, r: 0.9, rmsL: 0.85, rmsR: 0.85, lufs: -5 },
      tracks: [{ l: 0.5, r: 0.5, rmsL: 0.45, rmsR: 0.45, lufs: -10 }, { l: 0.3, r: 0.3, rmsL: 0.25, rmsR: 0.25, lufs: -15 }, { l: 0.1, r: 0.1, rmsL: 0.08, rmsR: 0.08, lufs: -25 }],
    });
    expect(useMeterStore.getState().tracks).toHaveLength(3);

    useMeterStore.getState().update({
      master: { l: 0.1, r: 0.1, rmsL: 0.08, rmsR: 0.08, lufs: -25 },
      tracks: [{ l: 0.8, r: 0.8, rmsL: 0.75, rmsR: 0.75, lufs: -6 }],
    });
    expect(useMeterStore.getState().tracks).toHaveLength(1);
    expect(useMeterStore.getState().master.l).toBe(0.1);
  });
});
