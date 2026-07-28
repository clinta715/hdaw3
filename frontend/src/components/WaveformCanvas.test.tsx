import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup, act } from "@testing-library/react";
import { WaveformCanvas } from "./WaveformCanvas";
import { rpc } from "../rpc";
import type { ClipSnapshot, WaveformPeaks } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkClip(clipId: number): ClipSnapshot {
  return {
    clipId,
    trackIndex: 0,
    name: `Clip ${clipId}`,
    sourceFile: `C:/audio/${clipId}.wav`,
    startBeat: 0,
    durationBeats: 4,
    offset: 0,
    gain: 1,
    fadeIn: 0,
    fadeOut: 0,
    looping: false,
    muted: false,
    isMidi: false,
    sourceBpm: 120,
    stretchMode: 0,
    stretchRatio: 1,
    sourceDuration: 4,
    isGhost: false,
    ghostSourceId: -1,
    gainEnvelope: [],
  };
}

const audiblePeaks: WaveformPeaks = {
  peaks: [-0.5, 0.5, -0.3, 0.4, -0.2, 0.6],
  sampleRate: 44100,
  numSamples: 1000,
};

const silentPeaks: WaveformPeaks = {
  peaks: [0, 0, 0, 0, 0, 0],
  sampleRate: 44100,
  numSamples: 1000,
};

// Flush the initial fetch plus every retry (3 x 500ms) and let React settle.
async function flush() {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(5000);
  });
}

describe("WaveformCanvas peak caching", () => {
  beforeEach(() => {
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    mockedCall.mockReset();
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  it("caches audible peaks so a remount does not re-fetch", async () => {
    mockedCall.mockResolvedValue(audiblePeaks);
    const clip = mkClip(200);

    const first = render(<WaveformCanvas clip={clip} width={100} height={40} />);
    await flush();
    const callsAfterFirst = mockedCall.mock.calls.length;
    expect(callsAfterFirst).toBe(1);
    first.unmount();

    render(<WaveformCanvas clip={clip} width={100} height={40} />);
    await flush();
    expect(mockedCall.mock.calls.length).toBe(callsAfterFirst);
  });

  it("does not cache silent peaks, so a remount re-fetches instead of sticking blank", async () => {
    mockedCall.mockResolvedValue(silentPeaks);
    const clip = mkClip(300);

    const first = render(<WaveformCanvas clip={clip} width={100} height={40} />);
    await flush();
    const callsAfterFirst = mockedCall.mock.calls.length;
    expect(callsAfterFirst).toBeGreaterThan(1);
    first.unmount();

    render(<WaveformCanvas clip={clip} width={100} height={40} />);
    await flush();
    expect(mockedCall.mock.calls.length).toBeGreaterThan(callsAfterFirst);
  });

  it("renders with a custom track color without crashing", async () => {
    mockedCall.mockResolvedValue(audiblePeaks);
    const clip = mkClip(400);
    const { container } = render(
      <WaveformCanvas clip={clip} width={100} height={40} color="#5b9bd5" />
    );
    await flush();
    expect(container.querySelector("canvas")).not.toBeNull();
  });
});
