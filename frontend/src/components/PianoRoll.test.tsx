import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { vi, describe, it, expect, beforeEach } from "vitest";
import PianoRoll from "./PianoRoll";

vi.mock("./NoteGrid", () => ({
  default: (props: any) => (
    <div data-testid="note-grid" data-clip-id={props.clipId} data-ppb={props.pixelsPerBeat}>
      <span>Notes: {props.notes.length}</span>
      {props.selectedNoteIds.size > 0 && <span data-testid="ng-selected">Selected: {props.selectedNoteIds.size}</span>}
    </div>
  ),
}));

vi.mock("./VelocityLane", () => ({
  default: (props: any) => (
    <div data-testid="velocity-lane">
      <span>Vel notes: {props.notes.length}</span>
    </div>
  ),
}));

vi.mock("./CCLane", () => ({
  default: (props: any) => (
    <div data-testid="cc-lane" data-cc={props.controllerNumber}>
      <span>CC{props.controllerNumber}</span>
      <button data-testid={`cc-remove-${props.controllerNumber}`} onClick={() => props.onRemove()}>Remove</button>
    </div>
  ),
}));

vi.mock("./NoteOperatorsPane", () => ({
  default: (props: any) => (
    <div data-testid="note-operators-pane">
      <span>Operators: {props.selectedNoteIds.size}</span>
    </div>
  ),
}));

vi.mock("./grooveUtils", () => ({
  quantizeWithGroove: vi.fn((v: number) => v),
}));

const mockSyncNotes = vi.fn();

let projectStoreState: any;
let uiStoreState: any;

vi.mock("../store/projectStore", () => ({
  useProjectStore: Object.assign(
    vi.fn((selector: any) => {
      const state = {
        snapshot: projectStoreState.snapshot,
        notesByClip: projectStoreState.notesByClip,
        syncNotes: mockSyncNotes,
      };
      return selector ? selector(state) : state;
    }),
    {
      getState: () => ({
        snapshot: projectStoreState.snapshot,
        notesByClip: projectStoreState.notesByClip,
        syncNotes: mockSyncNotes,
        setState: vi.fn(),
      }),
    }
  ),
  __esModule: true,
}));

vi.mock("../store/uiStore", () => ({
  useUiStore: Object.assign(
    vi.fn((selector: any) => {
      const state = {
        selectedClipIds: uiStoreState.selectedClipIds,
        snapEnabled: uiStoreState.snapEnabled ?? false,
        snapDivision: uiStoreState.snapDivision ?? 0.25,
      };
      return selector ? selector(state) : state;
    }),
    {
      getState: () => ({
        selectedClipIds: uiStoreState.selectedClipIds,
        snapEnabled: uiStoreState.snapEnabled ?? false,
        snapDivision: uiStoreState.snapDivision ?? 0.25,
      }),
    }
  ),
  __esModule: true,
}));

vi.mock("../rpc", () => ({
  rpc: {
    call: vi.fn().mockResolvedValue(undefined),
  },
}));

describe("PianoRoll", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    projectStoreState = {
      snapshot: {
        clips: [
          { clipId: 1, isMidi: true, name: "MIDI Clip 1", trackIndex: 0, gain: 1, looping: false, durationBeats: 4 },
          { clipId: 2, isMidi: true, name: "MIDI Clip 2", trackIndex: 0, gain: 0.8, looping: true, durationBeats: 8 },
          { clipId: 3, isMidi: false, name: "Audio Clip", trackIndex: 0, gain: 1, looping: false, durationBeats: 4 },
        ],
        tracks: [{ index: 0, name: "Track 1" }],
      },
      notesByClip: new Map(),
    };
    uiStoreState = {
      selectedClipIds: new Set<number>(),
      snapEnabled: false,
      snapDivision: 0.25,
    };
  });

  describe("Clip Selection", () => {
    it("shows 'No MIDI clips' when there are no clips", () => {
      projectStoreState.snapshot = { ...projectStoreState.snapshot, clips: [] };
      render(<PianoRoll />);
      expect(screen.getByText("No MIDI clips")).toBeInTheDocument();
    });

    it("shows 'No MIDI clips' when only audio clips exist", () => {
      projectStoreState.snapshot = {
        ...projectStoreState.snapshot,
        clips: [{ clipId: 3, isMidi: false, name: "Audio", trackIndex: 0 }],
      };
      render(<PianoRoll />);
      expect(screen.getByText("No MIDI clips")).toBeInTheDocument();
    });

    it("renders first MIDI clip by default", () => {
      render(<PianoRoll />);
      const select = screen.getByRole("combobox") as HTMLSelectElement;
      expect(select.value).toBe("1");
      expect(screen.getByTestId("note-grid")).toHaveAttribute("data-clip-id", "1");
    });

    it("timeline-selected clip takes priority over first clip", () => {
      uiStoreState.selectedClipIds = new Set([2]);
      render(<PianoRoll />);
      const select = screen.getByRole("combobox") as HTMLSelectElement;
      expect(select.value).toBe("2");
      expect(screen.getByTestId("note-grid")).toHaveAttribute("data-clip-id", "2");
    });

    it("internal selection fallback when timeline has non-MIDI clip", () => {
      uiStoreState.selectedClipIds = new Set([3]);
      render(<PianoRoll />);
      const select = screen.getByRole("combobox") as HTMLSelectElement;
      expect(select.value).toBe("1");
    });

    it("changing clip in dropdown loads notes", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      const select = screen.getByRole("combobox") as HTMLSelectElement;
      await user.selectOptions(select, "2");
      expect(mockSyncNotes).toHaveBeenCalled();
    });

    it("displays clip names in dropdown", () => {
      render(<PianoRoll />);
      expect(screen.getByText("MIDI Clip 1")).toBeInTheDocument();
      expect(screen.getByText("MIDI Clip 2")).toBeInTheDocument();
    });
  });

  describe("Note Grid Display", () => {
    it("NoteGrid renders with notes", () => {
      projectStoreState.notesByClip.set(1, [
        { noteId: 1, pitch: 60, startBeat: 0, durationBeats: 1, velocity: 100 },
        { noteId: 2, pitch: 64, startBeat: 1, durationBeats: 1, velocity: 80 },
      ]);
      render(<PianoRoll />);
      expect(screen.getByTestId("note-grid")).toHaveTextContent("Notes: 2");
    });

    it("NoteGrid renders with empty notes", () => {
      render(<PianoRoll />);
      expect(screen.getByTestId("note-grid")).toHaveTextContent("Notes: 0");
    });

    it("piano keys visible on left", () => {
      render(<PianoRoll />);
      const keys = document.querySelectorAll(".pr-key");
      expect(keys.length).toBe(128);
    });

    it("zoom controls affect pixels per beat", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      const grid = screen.getByTestId("note-grid");
      expect(grid).toHaveAttribute("data-ppb", "80");
      await user.click(screen.getByTitle("Zoom In (Ctrl+wheel to zoom)"));
      expect(grid).toHaveAttribute("data-ppb", "100");
    });

    it("zoom out decreases pixels per beat", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      const grid = screen.getByTestId("note-grid");
      await user.click(screen.getByTitle("Zoom Out (Ctrl+wheel to zoom)"));
      expect(grid).toHaveAttribute("data-ppb", "64");
    });
  });

  describe("Velocity Lane", () => {
    it("not rendered by default", () => {
      render(<PianoRoll />);
      expect(screen.queryByTestId("velocity-lane")).not.toBeInTheDocument();
    });

    it("renders when toggle is clicked", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByTitle("Toggle velocity lane"));
      expect(screen.getByTestId("velocity-lane")).toBeInTheDocument();
    });

    it("toggle off after opening", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      const btn = screen.getByTitle("Toggle velocity lane");
      await user.click(btn);
      expect(screen.getByTestId("velocity-lane")).toBeInTheDocument();
      await user.click(btn);
      expect(screen.queryByTestId("velocity-lane")).not.toBeInTheDocument();
    });
  });

  describe("CC Lanes", () => {
    it("Add CC lane button opens popover", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByTitle("Add CC lane"));
      expect(screen.getByText("Add")).toBeInTheDocument();
    });

    it("can add a CC lane", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByTitle("Add CC lane"));
      const selects = screen.getAllByRole("combobox");
      const ccSelect = selects[selects.length - 1];
      await user.selectOptions(ccSelect, "7");
      await user.click(screen.getByRole("button", { name: /^Add$/ }));
      expect(screen.getByTestId("cc-lane")).toHaveAttribute("data-cc", "7");
    });

    it("remove CC lane", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByTitle("Add CC lane"));
      const selects = screen.getAllByRole("combobox");
      await user.selectOptions(selects[selects.length - 1], "1");
      await user.click(screen.getByRole("button", { name: /^Add$/ }));
      expect(screen.getByTestId("cc-lane")).toBeInTheDocument();
      await user.click(screen.getByTestId("cc-remove-1"));
      expect(screen.queryByTestId("cc-lane")).not.toBeInTheDocument();
    });
  });

  describe("Chord Mode", () => {
    it("chord selector not visible by default", () => {
      render(<PianoRoll />);
      const selects = screen.getAllByRole("combobox");
      expect(selects.length).toBe(1);
    });

    it("chord selector visible when enabled", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByLabelText("Chord"));
      const selects = screen.getAllByRole("combobox");
      expect(selects.length).toBeGreaterThan(1);
    });

    it("chord type defaults to major", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByLabelText("Chord"));
      const selects = screen.getAllByRole("combobox");
      const chordSelect = selects[selects.length - 1];
      expect(chordSelect).toHaveValue("major");
    });
  });

  describe("Clip Controls", () => {
    it("gain slider appears in clip popover", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByRole("button", { name: "Clip controls" }));
      expect(screen.getByText("1.00x")).toBeInTheDocument();
    });

    it("transpose buttons exist in clip popover", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByRole("button", { name: "Clip controls" }));
      expect(screen.getByText("-12")).toBeInTheDocument();
      expect(screen.getByText("-1")).toBeInTheDocument();
      expect(screen.getByText("+1")).toBeInTheDocument();
      expect(screen.getByText("+12")).toBeInTheDocument();
    });

    it("clip quantize strength shows in popover", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByRole("button", { name: "Clip controls" }));
      expect(screen.getByText("100%")).toBeInTheDocument();
    });

    it("humanize button exists in popover", async () => {
      const user = userEvent.setup();
      render(<PianoRoll />);
      await user.click(screen.getByRole("button", { name: "Clip controls" }));
      expect(screen.getByText("Humanize All")).toBeInTheDocument();
    });
  });

  describe("Note Operators", () => {
    it("NoteOperatorsPane not rendered when no notes selected", () => {
      render(<PianoRoll />);
      expect(screen.queryByTestId("note-operators-pane")).not.toBeInTheDocument();
    });
  });

  describe("Zoom Controls", () => {
    it("zoom fit button exists", () => {
      render(<PianoRoll />);
      expect(screen.getByTitle("Fit Clip to View")).toBeInTheDocument();
    });
  });

  describe("Loop Toggle", () => {
    it("loop checkbox visible when clip active", () => {
      render(<PianoRoll />);
      expect(screen.getByText("Loop")).toBeInTheDocument();
    });
  });
});
