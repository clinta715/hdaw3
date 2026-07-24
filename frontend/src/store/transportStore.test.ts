import { describe, it, expect, beforeEach } from "vitest";
import { useTransportStore } from "../store/transportStore";

describe("transportStore", () => {
  beforeEach(() => {
    useTransportStore.setState({
      transport: {
        bpm: 120,
        isPlaying: false,
        isLooping: false,
        isRecording: false,
        loopStart: 0,
        loopEnd: 8,
        currentTimeSeconds: 0,
        sampleRate: 44100,
      },
    });
  });

  it("has correct defaults", () => {
    const state = useTransportStore.getState();
    expect(state.transport.bpm).toBe(120);
    expect(state.transport.isPlaying).toBe(false);
    expect(state.transport.isLooping).toBe(false);
    expect(state.transport.isRecording).toBe(false);
  });

  it("updates transport state", () => {
    useTransportStore.getState().update({
      bpm: 140,
      isPlaying: true,
      isLooping: true,
      isRecording: false,
      loopStart: 2,
      loopEnd: 10,
      currentTimeSeconds: 5,
      sampleRate: 48000,
    });

    const t = useTransportStore.getState().transport;
    expect(t.bpm).toBe(140);
    expect(t.isPlaying).toBe(true);
    expect(t.isLooping).toBe(true);
    expect(t.loopStart).toBe(2);
    expect(t.loopEnd).toBe(10);
    expect(t.sampleRate).toBe(48000);
  });

  it("preserves fields not in update", () => {
    useTransportStore.getState().update({
      bpm: 90,
      isPlaying: false,
      isLooping: false,
      isRecording: false,
      loopStart: 0,
      loopEnd: 8,
      currentTimeSeconds: 0,
      sampleRate: 44100,
    });

    expect(useTransportStore.getState().transport.bpm).toBe(90);
  });
});
