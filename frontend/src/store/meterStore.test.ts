import { describe, it, expect, beforeEach } from "vitest";
import { useMeterStore } from "../store/meterStore";

describe("meterStore", () => {
  beforeEach(() => {
    useMeterStore.setState({
      master: { l: 0, r: 0 },
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
      master: { l: 0.5, r: 0.6 },
      tracks: [
        { l: 0.1, r: 0.2 },
        { l: 0.3, r: 0.4 },
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
      master: { l: 0.9, r: 0.9 },
      tracks: [{ l: 0.5, r: 0.5 }, { l: 0.3, r: 0.3 }, { l: 0.1, r: 0.1 }],
    });
    expect(useMeterStore.getState().tracks).toHaveLength(3);

    useMeterStore.getState().update({
      master: { l: 0.1, r: 0.1 },
      tracks: [{ l: 0.8, r: 0.8 }],
    });
    expect(useMeterStore.getState().tracks).toHaveLength(1);
    expect(useMeterStore.getState().master.l).toBe(0.1);
  });
});
