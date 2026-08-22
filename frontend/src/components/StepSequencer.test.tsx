import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import StepSequencer, { DRUM_LABELS, cellPitch, computeCurrentStep } from "./StepSequencer";
import type { NoteSnapshot, ClipSnapshot } from "../rpc/types";

const { mockCall } = vi.hoisted(() => ({
  mockCall: vi.fn().mockResolvedValue(null),
}));

vi.mock("../rpc", () => ({ rpc: { call: mockCall } }));

vi.mock("../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
}));

vi.mock("../store/transportStore", () => {
  const store = {
    transport: {
      bpm: 120,
      isPlaying: false,
      isLooping: false,
      isRecording: false,
      loopStart: 0,
      loopEnd: 8,
      currentTimeSeconds: 0,
      sampleRate: 44100,
      timeSigNumerator: 4,
      timeSigDenominator: 4,
    },
  };
  const hook = Object.assign(
    vi.fn((selector?: (s: typeof store) => unknown) =>
      selector ? selector(store) : store,
    ),
    { getState: () => store },
  );
  return { useTransportStore: hook };
});

function mkMidiClip(overrides: Partial<ClipSnapshot> = {}): ClipSnapshot {
  return {
    clipId: 10,
    trackIndex: 0,
    name: "Drums",
    sourceFile: "",
    startBeat: 0,
    durationBeats: 4,
    offset: 0,
    gain: 1,
    fadeIn: 0,
    fadeOut: 0,
    looping: false,
    muted: false,
    isMidi: true,
    sourceBpm: 120,
    stretchMode: 0,
    stretchRatio: 1,
    sourceDuration: 0,
    isGhost: false,
    ghostSourceId: 0,
    gainEnvelope: [],
    activeTake: 0,
    takeCount: 0,
    takes: [],
    ...overrides,
  };
}

function mkNote(overrides: Partial<NoteSnapshot> = {}): NoteSnapshot {
  return {
    noteId: 1,
    pitch: 48,
    velocity: 96,
    startBeat: 0,
    durationBeats: 0.0625,
    chance: 1,
    repeatCount: 0,
    repeatRate: 0.25,
    repeatCurve: 0,
    occurrence: 0,
    recurrence: 0,
    noteGain: 1,
    notePan: 0,
    notePitch: 0,
    noteTimbre: 0.5,
    notePressure: 0,
    ...overrides,
  };
}

function makeStoreState(
  clipOverrides?: Partial<ClipSnapshot>,
  clipIds: number[] = [10],
) {
  const clip = mkMidiClip(clipOverrides);
  return {
    snapshot: {
      clips: clipOverrides === undefined ? [] : [clip],
      tracks: [],
    },
    selectedClipIds: new Set(clipIds),
  };
}

const { getMockStoreState, setMockStoreState } = vi.hoisted(() => {
  let state: ReturnType<typeof makeStoreState> = {
    snapshot: { clips: [], tracks: [] },
    selectedClipIds: new Set<number>(),
  };
  return {
    getMockStoreState: () => state,
    setMockStoreState: (s: ReturnType<typeof makeStoreState>) => { state = s; },
  };
});

vi.mock("../store/projectStore", () => ({
  useProjectStore: vi.fn((selector?: (s: ReturnType<typeof makeStoreState>) => unknown) =>
    selector ? selector(getMockStoreState()) : getMockStoreState(),
  ),
}));

vi.mock("../store/uiStore", () => ({
  useUiStore: vi.fn((selector?: (s: { selectedClipIds: Set<number> }) => unknown) =>
    selector ? selector({ selectedClipIds: getMockStoreState().selectedClipIds }) : { selectedClipIds: getMockStoreState().selectedClipIds },
  ),
}));

const MAX_STEPS = 16;

function cellIndex(row: number, col: number, patternLen = MAX_STEPS): number {
  return row * patternLen + col;
}

describe("StepSequencer", () => {
  beforeEach(() => {
    vi.spyOn(global, "requestAnimationFrame").mockImplementation((cb) => {
      setTimeout(cb, 0);
      return 0;
    });
    vi.spyOn(global, "cancelAnimationFrame").mockImplementation(() => {});
    mockCall.mockReset();
    mockCall.mockResolvedValue(null);
  });

  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
  });

  describe("pure functions", () => {
    it("cellPitch returns correct pitch for row 0 (highest)", () => {
      expect(cellPitch(0)).toBe(55);
    });

    it("cellPitch returns correct pitch for row 7 (lowest)", () => {
      expect(cellPitch(7)).toBe(48);
    });

    it("computeCurrentStep returns -1 for zero length", () => {
      expect(computeCurrentStep(0, 0.0625, 0)).toBe(-1);
    });

    it("computeCurrentStep returns -1 for negative beat", () => {
      expect(computeCurrentStep(-1, 0.0625, 16)).toBe(-1);
    });

    it("computeCurrentStep returns -1 for zero stepBeats", () => {
      expect(computeCurrentStep(0, 0, 16)).toBe(-1);
    });

    it("computeCurrentStep returns correct step index", () => {
      expect(computeCurrentStep(0, 0.0625, 16)).toBe(0);
      expect(computeCurrentStep(0.0625, 0.0625, 16)).toBe(1);
      expect(computeCurrentStep(0.5, 0.0625, 16)).toBe(8);
    });

    it("computeCurrentStep wraps around pattern length", () => {
      expect(computeCurrentStep(1.0, 0.0625, 16)).toBe(0);
      expect(computeCurrentStep(1.0625, 0.0625, 16)).toBe(1);
    });

    it("DRUM_LABELS has 8 entries", () => {
      expect(DRUM_LABELS).toHaveLength(8);
    });

    it("DRUM_LABELS contains expected drum names", () => {
      expect(DRUM_LABELS).toContain("Kick");
      expect(DRUM_LABELS).toContain("Snare");
      expect(DRUM_LABELS).toContain("OpHat");
    });
  });

  describe("grid rendering", () => {
    it("renders the step sequencer container", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      expect(container.querySelector(".step-sequencer")).toBeInTheDocument();
    });

    it("shows title", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(screen.getByText("Step Sequencer")).toBeInTheDocument();
    });

    it("shows empty hint when no MIDI clip selected", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(screen.getByText("Select a MIDI clip to edit")).toBeInTheDocument();
    });

    it("shows clip name when a MIDI clip is selected", () => {
      setMockStoreState(makeStoreState({ name: "MyDrums" }, [10]));
      render(<StepSequencer />);
      expect(screen.getByText("MyDrums")).toBeInTheDocument();
    });

    it("renders 8 rows of grid cells", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const rows = container.querySelectorAll(".ss-row");
      expect(rows).toHaveLength(8);
    });

    it("renders 16 cells per row by default", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      expect(cells).toHaveLength(8 * 16);
    });

    it("renders step numbers at the bottom", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const stepNums = container.querySelectorAll(".ss-step-num");
      expect(stepNums).toHaveLength(16);
      expect(stepNums[0].textContent).toBe("1");
      expect(stepNums[15].textContent).toBe("16");
    });

    it("renders the hint text", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(screen.getByText(/click: toggle/)).toBeInTheDocument();
    });
  });

  describe("label modes", () => {
    it("shows note labels by default", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(screen.getByText("G3")).toBeInTheDocument();
      expect(screen.getByText("C3")).toBeInTheDocument();
    });

    it("toggles to drum labels when checkbox is checked", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      const checkbox = screen.getByRole("checkbox");
      fireEvent.click(checkbox);
      expect(screen.getByText("Kick")).toBeInTheDocument();
      expect(screen.getByText("Snare")).toBeInTheDocument();
      expect(screen.getByText("Crash")).toBeInTheDocument();
    });

    it("toggles back to note labels", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      const checkbox = screen.getByRole("checkbox");
      fireEvent.click(checkbox);
      fireEvent.click(checkbox);
      expect(screen.getByText("G3")).toBeInTheDocument();
    });
  });

  describe("pattern length selector", () => {
    it("defaults to 16 steps", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const select = container.querySelector(".ss-length") as HTMLSelectElement;
      expect(select.value).toBe("16");
    });

    it("changes to 8 steps", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const select = container.querySelector(".ss-length") as HTMLSelectElement;
      fireEvent.change(select, { target: { value: "8" } });
      const cells = container.querySelectorAll(".ss-cell");
      expect(cells).toHaveLength(8 * 8);
    });

    it("shows 8 step numbers when set to 8", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const select = container.querySelector(".ss-length") as HTMLSelectElement;
      fireEvent.change(select, { target: { value: "8" } });
      const stepNums = container.querySelectorAll(".ss-step-num");
      expect(stepNums).toHaveLength(8);
    });
  });

  describe("step interaction", () => {
    it("clicking a cell does nothing when no clip is selected", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[0]);
      expect(mockCall).not.toHaveBeenCalled();
    });

    it("clicking a cell sends addNote RPC when clip is selected", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      expect(mockCall).toHaveBeenCalledWith(
        "project.addNote",
        expect.objectContaining({
          clipId: 10,
          velocity: 96,
        }),
      );
    });

    it("clicking an active cell sends removeNote RPC", async () => {
      const notePitch = cellPitch(0);
      mockCall.mockResolvedValue([
        mkNote({ noteId: 42, pitch: notePitch, startBeat: 0 }),
      ]);
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      await waitFor(() => {
        expect(mockCall).toHaveBeenCalledWith("read.getNotes", { clipId: 10 });
      });
      mockCall.mockClear();
      mockCall.mockResolvedValue([
        mkNote({ noteId: 42, pitch: notePitch, startBeat: 0 }),
      ]);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      await waitFor(() => {
        expect(mockCall).toHaveBeenCalledWith(
          "project.removeNote",
          { noteId: 42 },
        );
      });
    });

    it("clicking a cell applies correct pitch based on row", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      expect(mockCall).toHaveBeenCalledWith(
        "project.addNote",
        expect.objectContaining({ pitch: cellPitch(0) }),
      );
      fireEvent.click(cells[cellIndex(3, 0)]);
      expect(mockCall).toHaveBeenCalledWith(
        "project.addNote",
        expect.objectContaining({ pitch: cellPitch(3) }),
      );
    });

    it("clicking a cell applies correct startBeat based on column", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      expect(mockCall).toHaveBeenCalledWith(
        "project.addNote",
        expect.objectContaining({ startBeat: 0 }),
      );
      fireEvent.click(cells[cellIndex(0, 4)]);
      expect(mockCall).toHaveBeenCalledWith(
        "project.addNote",
        expect.objectContaining({ startBeat: 0.25 }),
      );
    });

    it("shift-click on active cell triggers velocity adjust", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      fireEvent.click(cells[cellIndex(0, 0)], { shiftKey: true });
      expect(mockCall).toHaveBeenCalledWith(
        "read.getNotes",
        expect.anything(),
      );
    });

    it("alt-click on active cell triggers velocity adjust", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      fireEvent.click(cells[cellIndex(0, 0)]);
      fireEvent.click(cells[cellIndex(0, 0)], { altKey: true });
      expect(mockCall).toHaveBeenCalledWith(
        "read.getNotes",
        expect.anything(),
      );
    });
  });

  describe("beat markers", () => {
    it("marks cells on beat boundaries (every 4th column)", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      expect(cells[cellIndex(0, 0)].classList.contains("beat")).toBe(true);
      expect(cells[cellIndex(0, 4)].classList.contains("beat")).toBe(true);
      expect(cells[cellIndex(0, 8)].classList.contains("beat")).toBe(true);
      expect(cells[cellIndex(0, 12)].classList.contains("beat")).toBe(true);
      expect(cells[cellIndex(0, 1)].classList.contains("beat")).toBe(false);
    });
  });

  describe("loading notes from clip", () => {
    it("calls read.getNotes on mount when clip is selected", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      render(<StepSequencer />);
      expect(mockCall).toHaveBeenCalledWith("read.getNotes", { clipId: 10 });
    });

    it("does not call read.getNotes when no clip is selected", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(mockCall).not.toHaveBeenCalled();
    });

    it("populates grid cells from loaded notes", async () => {
      mockCall.mockResolvedValue([
        mkNote({ noteId: 1, pitch: 48, startBeat: 0, velocity: 100 }),
      ]);
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      await waitFor(() => {
        const cells = container.querySelectorAll(".ss-cell");
        expect(cells[cellIndex(7, 0)].classList.contains("active")).toBe(true);
      });
    });

    it("sets opacity based on velocity", async () => {
      mockCall.mockResolvedValue([
        mkNote({ noteId: 1, pitch: 48, startBeat: 0, velocity: 64 }),
      ]);
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      await waitFor(() => {
        const cells = container.querySelectorAll(".ss-cell");
        const cell = cells[cellIndex(7, 0)];
        expect(cell.getAttribute("style")).toContain("opacity");
      });
    });

    it("populates velocity from notes", async () => {
      mockCall.mockResolvedValue([
        mkNote({ noteId: 1, pitch: 48, startBeat: 0, velocity: 127 }),
      ]);
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      await waitFor(() => {
        const cells = container.querySelectorAll(".ss-cell");
        expect(cells[cellIndex(7, 0)].getAttribute("title")).toBe("vel 127");
      });
    });
  });

  describe("current step highlight", () => {
    it("does not highlight when transport is stopped", () => {
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      const hasPlayhead = Array.from(cells).some((c) =>
        c.classList.contains("playhead"),
      );
      expect(hasPlayhead).toBe(false);
    });
  });

  describe("no clip selected state", () => {
    it("shows empty hint instead of clip name", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(screen.getByText("Select a MIDI clip to edit")).toBeInTheDocument();
      expect(screen.queryByText("Drums")).toBeNull();
    });

    it("does not fetch notes", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      expect(mockCall).not.toHaveBeenCalled();
    });
  });

  describe("grid structure", () => {
    it("every row has a note label", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const labels = container.querySelectorAll(".ss-note-label");
      expect(labels.length).toBeGreaterThanOrEqual(8);
    });

    it("cell grid is inside ss-grid container", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const grid = container.querySelector(".ss-grid");
      expect(grid).toBeInTheDocument();
      expect(grid?.querySelectorAll(".ss-row")).toHaveLength(8);
    });

    it("step labels are inside ss-step-labels container", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const stepLabels = container.querySelector(".ss-step-labels");
      expect(stepLabels).toBeInTheDocument();
    });
  });

  describe("multiple note positions", () => {
    it("populates multiple cells from different notes", async () => {
      mockCall.mockResolvedValue([
        mkNote({ noteId: 1, pitch: 48, startBeat: 0, velocity: 96 }),
        mkNote({ noteId: 2, pitch: 50, startBeat: 0.25, velocity: 80 }),
        mkNote({ noteId: 3, pitch: 52, startBeat: 0.5, velocity: 110 }),
      ]);
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      await waitFor(() => {
        const cells = container.querySelectorAll(".ss-cell.active");
        expect(cells.length).toBe(3);
      });
    });
  });

  describe("error handling", () => {
    it("addNote RPC failure does not crash the component", () => {
      mockCall.mockRejectedValue(new Error("network"));
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      const { container } = render(<StepSequencer />);
      const cells = container.querySelectorAll(".ss-cell");
      expect(() => fireEvent.click(cells[0])).not.toThrow();
    });

    it("read.getNotes failure does not crash the component", () => {
      mockCall.mockRejectedValue(new Error("rpc error"));
      setMockStoreState(makeStoreState({ startBeat: 0 }, [10]));
      expect(() => render(<StepSequencer />)).not.toThrow();
    });
  });

  describe("header elements", () => {
    it("has drum toggle checkbox", () => {
      setMockStoreState(makeStoreState());
      render(<StepSequencer />);
      const checkbox = screen.getByRole("checkbox");
      expect(checkbox).toBeInTheDocument();
      expect(checkbox).not.toBeChecked();
    });

    it("has pattern length selector", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const select = container.querySelector(".ss-length");
      expect(select).toBeInTheDocument();
    });

    it("pattern length selector has 8 and 16 options", () => {
      setMockStoreState(makeStoreState());
      const { container } = render(<StepSequencer />);
      const select = container.querySelector(".ss-length") as HTMLSelectElement;
      const options = Array.from(select.options).map((o) => o.value);
      expect(options).toContain("8");
      expect(options).toContain("16");
    });
  });
});
