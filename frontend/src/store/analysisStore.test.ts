import { describe, it, expect, beforeEach } from "vitest";
import { useAnalysisStore } from "./analysisStore";
import { FmAnalysisPayload } from "../rpc/types";

describe("analysisStore", () => {
  beforeEach(() => {
    useAnalysisStore.setState({ tracks: [] });
  });

  it("starts with empty tracks", () => {
    expect(useAnalysisStore.getState().tracks).toEqual([]);
  });

  it("updates tracks from payload", () => {
    const payload: FmAnalysisPayload = {
      tracks: [
        { opEgLevel: [0.8, 0.3, 0, 0, 0, 0], activeVoices: 2, algorithm: 5 },
        { opEgLevel: [0, 0, 0, 0, 0, 0], activeVoices: 0, algorithm: 0 },
      ],
    };
    useAnalysisStore.getState().update(payload);
    const state = useAnalysisStore.getState();
    expect(state.tracks).toHaveLength(2);
    expect(state.tracks[0].activeVoices).toBe(2);
    expect(state.tracks[0].opEgLevel[0]).toBeCloseTo(0.8);
    expect(state.tracks[0].algorithm).toBe(5);
  });

  it("overwrites on subsequent updates", () => {
    const p1: FmAnalysisPayload = {
      tracks: [{ opEgLevel: [1, 0, 0, 0, 0, 0], activeVoices: 1, algorithm: 0 }],
    };
    const p2: FmAnalysisPayload = {
      tracks: [{ opEgLevel: [0, 0.5, 0, 0, 0, 0], activeVoices: 3, algorithm: 10 }],
    };
    useAnalysisStore.getState().update(p1);
    useAnalysisStore.getState().update(p2);
    const state = useAnalysisStore.getState();
    expect(state.tracks[0].activeVoices).toBe(3);
    expect(state.tracks[0].opEgLevel[1]).toBeCloseTo(0.5);
    expect(state.tracks[0].algorithm).toBe(10);
  });
});
