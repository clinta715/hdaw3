import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act, waitFor } from "@testing-library/react";
import PhraseGeneratorDialog from "./PhraseGeneratorDialog";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import type { ProjectSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

// Minimal valid ProjectSnapshot (shape from rpc/types.ts ProjectSnapshot).
const MINIMAL_SNAPSHOT: ProjectSnapshot = {
  name: "Test Project",
  transport: {
    bpm: 120,
    isPlaying: false,
    isLooping: false,
    isRecording: false,
    loopStart: 0,
    loopEnd: 0,
    currentTimeSeconds: 0,
    sampleRate: 44100,
    timeSigNumerator: 4,
    timeSigDenominator: 4,
  },
  tracks: [
    {
      index: 0,
      name: "Track 1",
      color: 0,
      volume: 0.8,
      pan: 0,
      muted: false,
      soloed: false,
      armed: false,
      inputMonitor: false,
      height: 120,
      midiChannel: 1,
      clipCount: 0,
      trackType: 1,
      effectiveMuted: false,
      effectiveSoloed: false,
    },
  ],
  clips: [],
  scaleRoot: 0,
  scaleMode: 0,
};

describe("PhraseGeneratorDialog rhythm mode", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockImplementation(async (method: string) => {
      // Metadata reads must resolve to arrays — a bare {} would crash the
      // shared "Scale" select's .map() when the metadata effect sets state.
      if (
        method === "composition.getScaleModes" ||
        method === "composition.getChordTypes" ||
        method === "composition.getProgressionPatterns" ||
        method === "composition.getStyleNames"
      ) {
        return [];
      }
      return {};
    });
    useProjectStore.setState({
      snapshot: MINIMAL_SNAPSHOT,
      isDirty: false,
    });
  });

  afterEach(() => {
    cleanup();
  });

  it("shows the Rhythm mode option and its controls", async () => {
    render(<PhraseGeneratorDialog onClose={vi.fn()} />);
    await flushRead();
    expect(screen.getByRole("option", { name: "Rhythm" })).toBeInTheDocument();
  });

  it("sends a single generateRhythmPattern call with pulse params when generating", async () => {
    render(<PhraseGeneratorDialog onClose={vi.fn()} />);
    await flushRead();
    fireEvent.change(screen.getByRole("combobox", { name: "Mode" }), { target: { value: "4" } });
    fireEvent.change(screen.getByPlaceholderText('E.g. "E(3,8,1) [x-]x2"'), {
      target: { value: "E(3,8)" },
    });
    mockedCall.mockResolvedValue({ clipId: 7, noteCount: 3 });
    fireEvent.click(screen.getByRole("button", { name: "Generate" }));
    await waitFor(() => {
      const call = mockedCall.mock.calls.find((c) => c[0] === "composition.generateRhythmPattern");
      expect(call).toBeTruthy();
      const args = call![1] as Record<string, unknown>;
      expect(args.trackIndex).toBe(0);
      expect(args.pulseA).toBe(4);
      expect(args.pulseB).toBe(3);
      expect(args.dsl).toBe("E(3,8)");
    });
  });
});
