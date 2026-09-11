import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act, waitFor } from "@testing-library/react";
import SongPlanPanel from "./SongPlanPanel";
import { rpc } from "../../rpc";
import { useProjectStore } from "../../store/projectStore";
import type { ProjectSnapshot } from "../../rpc/types";

vi.mock("../../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
    await Promise.resolve();
  });
}

const MINIMAL_SNAPSHOT = {
  name: "T", transport: { bpm: 140, isPlaying: false, isLooping: false, isRecording: false,
    loopStart: 0, loopEnd: 0, currentTimeSeconds: 0, sampleRate: 44100,
    timeSigNumerator: 4, timeSigDenominator: 4 },
  tracks: [{ index: 0, name: "Track 1", color: 0, volume: 0.8, pan: 0.5, muted: false,
    soloed: false, armed: false, inputMonitor: false, height: 64, midiChannel: 1, clipCount: 0,
    trackType: 0, effectiveMuted: false, effectiveSoloed: false },
  { index: 1, name: "Synth", color: 1, volume: 0.8, pan: 0.5, muted: false, soloed: false,
    armed: false, inputMonitor: false, height: 64, midiChannel: 2, clipCount: 0,
    trackType: 1, effectiveMuted: false, effectiveSoloed: false }],
  clips: [], scaleRoot: 0, scaleMode: 0,
} as unknown as ProjectSnapshot;

beforeEach(() => {
  mockedCall.mockReset();
  mockedCall.mockImplementation(async (method: string) => {
    if (method === "composition.getSongPlan") return { hasPlan: false };
    if (method === "composition.getCells") return { cells: [] };
    if (method === "composition.listSectionTemplates") return { templates: [] };
    return {};
  });
  useProjectStore.setState({ snapshot: MINIMAL_SNAPSHOT });
});

afterEach(() => cleanup());

describe("SongPlanPanel", () => {
  it("renders the plan editor with default sections", async () => {
    render(<SongPlanPanel />);
    await flushRead();
    expect(screen.getByTestId("song-plan-panel")).toBeInTheDocument();
    expect(screen.getByLabelText("Plan BPM")).toHaveValue(140);
    expect(screen.getByTestId("section-row-0")).toBeInTheDocument();
    expect(screen.getByTestId("section-row-2")).toBeInTheDocument();
  });

  it("applies the plan with bars summed into totalBars", async () => {
    render(<SongPlanPanel />);
    await flushRead();
    fireEvent.click(screen.getByTestId("apply-plan"));
    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("composition.setSongPlan", expect.objectContaining({
        bpm: 140, keyRoot: 5, scaleMode: 7, style: "full-on", seed: 0, totalBars: 32,
        sections: [
          { name: "intro", kind: "intro", bars: 8 },
          { name: "build", kind: "build", bars: 8 },
          { name: "main", kind: "mainA", bars: 16 },
        ],
      }));
    });
  });

  it("adds a cell recipe via the exact RPC payload", async () => {
    render(<SongPlanPanel />);
    await flushRead();
    fireEvent.click(screen.getByTestId("add-cell"));
    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("composition.setCellRecipe", expect.objectContaining({
        section: "intro", role: "bass", trackId: 1, source: "phrase", seed: 0, locked: false,
      }));
    });
  });

  it("style picker patches the cell params JSON before setCellRecipe", async () => {
    mockedCall.mockImplementation(async (method: string) => {
      if (method === "composition.getSongPlan") return { hasPlan: false };
      if (method === "composition.getCells") return { cells: [] };
      if (method === "composition.listSectionTemplates") return { templates: [] };
      if (method === "composition.getStyleNames") return [{ name: "BassLine" }, { name: "Lead" }];
      if (method === "composition.listPatterns") return [];
      return {};
    });
    render(<SongPlanPanel />);
    await flushRead();
    const sel = await screen.findByTestId("style-picker");
    fireEvent.change(sel, { target: { value: "Lead" } });
    fireEvent.click(screen.getByTestId("add-cell"));
    await waitFor(() => expect(mockedCall).toHaveBeenCalledWith("composition.setCellRecipe",
      expect.objectContaining({ section: "intro", source: "phrase",
        params: JSON.stringify({ style: "Lead" }) })));
  });

  it("shows cells and sends fill + reroll", async () => {
    mockedCall.mockImplementation(async (method: string) => {
      if (method === "composition.getSongPlan") return {
        hasPlan: true, bpm: 138, keyRoot: 5, scaleMode: 7, style: "dark", seed: 9, totalBars: 24,
        sections: [{ name: "drop", kind: "mainA", startBeat: 0, endBeat: 64 }],
      };
      if (method === "composition.getCells") return { cells: [
        { section: "drop", role: "arp", trackId: 1, source: "phrase", paramsJson: "{}",
          seed: 0, locked: false, lastClipId: -1, lastSeed: 0 },
      ] };
      if (method === "composition.listSectionTemplates") return { templates: [] };
      return {};
    });
    render(<SongPlanPanel />);
    await flushRead();
    // Section names live in <input value=...>, so assert via toHaveValue.
    expect(screen.getByTestId("cell-row-0")).toHaveTextContent(/drop/);
    expect(screen.getByLabelText("Section name 1")).toHaveValue("drop");

    fireEvent.click(screen.getByTestId("fill-cells"));
    await waitFor(() => expect(mockedCall).toHaveBeenCalledWith("composition.fillCells", { mode: "all" }));
    fireEvent.click(screen.getByTitle("Reroll cell"));
    await waitFor(() => expect(mockedCall).toHaveBeenCalledWith("composition.rerollCells", { section: "drop", role: "arp" }));
  });
});
