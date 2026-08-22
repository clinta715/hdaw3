import { describe, it, expect, beforeEach, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import TransportBar from "./TransportBar";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useTransportExtrasStore } from "../store/transportExtrasStore";
import { useBrowserStore } from "../store/browserStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue({}) },
}));

vi.mock("../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
}));

vi.mock("./FileMenu", () => ({ default: () => <div data-testid="file-menu" /> }));
vi.mock("./PluginManagerDialog", () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="plugin-manager">
      <button onClick={onClose}>close</button>
    </div>
  ),
}));
vi.mock("./PreferencesDialog", () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="prefs-dialog">
      <button onClick={onClose}>close</button>
    </div>
  ),
}));
vi.mock("./PhraseGeneratorDialog", () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="phrase-gen">
      <button onClick={onClose}>close</button>
    </div>
  ),
}));
vi.mock("./Icons", () => ({
  FolderIcon: () => <span>📁</span>,
  KnobIcon: () => <span>🎛</span>,
  NoteIcon: () => <span>♪</span>,
  SlidersIcon: () => <span>☰</span>,
}));

const mockRpc = vi.mocked((await import("../rpc")).rpc);

function baseTransport() {
  return {
    bpm: 120,
    isPlaying: false,
    isLooping: false,
    isRecording: false,
    punchEnabled: false,
    loopStart: 0,
    loopEnd: 8,
    currentTimeSeconds: 0,
    sampleRate: 44100,
    timeSigNumerator: 4,
    timeSigDenominator: 4,
  };
}

beforeEach(() => {
  vi.clearAllMocks();
  useTransportStore.setState({ transport: baseTransport() });
  useProjectStore.setState({
    snapshot: {
      name: "Test",
      transport: baseTransport(),
      tracks: [],
      clips: [],
      scaleRoot: 0,
      scaleMode: 0,
    },
    notesByClip: new Map(),
    lastSync: 0,
    isDirty: false,
    filePath: null,
    pendingTempIds: new Set(),
    pendingResolution: new Map(),
    recentProjects: [],
    loadingProject: false,
    loadProgress: null,
  } as any);
  useUiStore.setState({
    selectedClipIds: new Set(),
    lastSelectedClipId: null,
    selectedTrackIndex: null,
    clipClipboard: [],
    activeBottomTab: "mixer",
    snapEnabled: false,
    snapDivision: 1,
    snapGridOffset: false,
    snapToEvents: false,
    showPhraseGenerator: false,
    viewMode: "arrange",
    statusHint: null,
  } as any);
  useTransportExtrasStore.setState({ metronomeEnabled: false, followPlayhead: false });
  useBrowserStore.setState({ visible: false } as any);
  // Mock getScaleModes RPC call in useEffect
  mockRpc.call.mockImplementation((method: string) => {
    if (method === "composition.getScaleModes") return Promise.resolve([]);
    return Promise.resolve({});
  });
});

describe("TransportBar", () => {
  describe("transport button clicks → RPC calls", () => {
    it("rewind calls transport.rewind", async () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Rewind"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.rewind");
    });

    it("play calls transport.play when stopped", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Play"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.play");
    });

    it("pause calls transport.pause when playing", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), isPlaying: true } });
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Pause"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.pause");
    });

    it("stop calls transport.stop", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Stop"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.stop");
    });

    it("record calls transport.record", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Record"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.record");
    });

    it("loop toggle calls transport.toggleLoop", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Toggle Loop"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.toggleLoop");
    });

    it("punch toggle calls transport.setPunchEnabled", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Punch In/Out (uses loop region)"));
      expect(mockRpc.call).toHaveBeenCalledWith("transport.setPunchEnabled", { enabled: true });
    });

    it("undo calls project.undo and sets isDirty", async () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Undo (Ctrl+Z)"));
      await vi.waitFor(() => {
        expect(useProjectStore.getState().isDirty).toBe(true);
      });
      expect(mockRpc.call).toHaveBeenCalledWith("project.undo");
    });

    it("redo calls project.redo and sets isDirty", async () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Redo (Ctrl+Shift+Z)"));
      await vi.waitFor(() => {
        expect(useProjectStore.getState().isDirty).toBe(true);
      });
      expect(mockRpc.call).toHaveBeenCalledWith("project.redo");
    });
  });

  describe("BPM editing", () => {
    it("double-click BPM shows inline input", () => {
      render(<TransportBar />);
      const bpmSpan = screen.getByText("120.0 BPM");
      fireEvent.doubleClick(bpmSpan);
      expect(screen.getByDisplayValue("120.0")).toBeInTheDocument();
    });

    it("Enter on BPM input commits via project.setTempo", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("120.0 BPM"));
      const input = screen.getByDisplayValue("120.0");
      await user.clear(input);
      await user.type(input, "140");
      await user.keyboard("{Enter}");
      expect(mockRpc.call).toHaveBeenCalledWith("project.setTempo", { bpm: 140 });
    });

    it("Escape cancels BPM edit", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("120.0 BPM"));
      const input = screen.getByDisplayValue("120.0");
      await user.keyboard("{Escape}");
      expect(screen.queryByDisplayValue("120.0")).not.toBeInTheDocument();
      expect(screen.getByText("120.0 BPM")).toBeInTheDocument();
    });

    it("blur commits BPM value", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("120.0 BPM"));
      const input = screen.getByDisplayValue("120.0");
      await user.clear(input);
      await user.type(input, "100");
      fireEvent.blur(input);
      expect(mockRpc.call).toHaveBeenCalledWith("project.setTempo", { bpm: 100 });
    });

    it("defaults to 120 when input is invalid", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("120.0 BPM"));
      const input = screen.getByDisplayValue("120.0");
      await user.clear(input);
      fireEvent.blur(input);
      expect(mockRpc.call).toHaveBeenCalledWith("project.setTempo", { bpm: 120 });
    });
  });

  describe("time signature editing", () => {
    it("double-click shows numerator/denominator inputs", () => {
      render(<TransportBar />);
      const ts = screen.getByText("4/4");
      fireEvent.doubleClick(ts);
      const inputs = screen.getAllByDisplayValue("4");
      expect(inputs.length).toBe(2);
    });

    it("Enter commits time signature via project.setTimeSignature", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("4/4"));
      const inputs = screen.getAllByDisplayValue("4");
      await user.clear(inputs[0]);
      await user.type(inputs[0], "3");
      await user.keyboard("{Enter}");
      expect(mockRpc.call).toHaveBeenCalledWith("project.setTimeSignature", {
        numerator: 3,
        denominator: 4,
      });
    });

    it("Escape cancels time signature edit", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("4/4"));
      await user.keyboard("{Escape}");
      expect(screen.getByText("4/4")).toBeInTheDocument();
    });

    it("blur commits time signature", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.doubleClick(screen.getByText("4/4"));
      const inputs = screen.getAllByDisplayValue("4");
      await user.clear(inputs[0]);
      await user.type(inputs[0], "6");
      fireEvent.blur(inputs[0]);
      expect(mockRpc.call).toHaveBeenCalledWith("project.setTimeSignature", {
        numerator: 6,
        denominator: 4,
      });
    });
  });

  describe("key/scale popover", () => {
    it("click key/scale button opens popover", () => {
      render(<TransportBar />);
      const btn = screen.getByTitle("Project key and scale");
      fireEvent.click(btn);
      expect(screen.getByLabelText("Key root")).toBeInTheDocument();
      expect(screen.getByLabelText("Scale mode")).toBeInTheDocument();
    });

    it("select root calls project.setScaleRoot", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Project key and scale"));
      const keySelect = screen.getByLabelText("Key root");
      await user.selectOptions(keySelect, "3");
      expect(mockRpc.call).toHaveBeenCalledWith("project.setScaleRoot", { root: 3 });
    });

    it("select mode calls project.setScaleMode", async () => {
      const user = userEvent.setup();
      const scaleModes = [{ index: 0, name: "Major" }, { index: 1, name: "Minor" }];
      mockRpc.call.mockImplementation((method: string) => {
        if (method === "composition.getScaleModes") return Promise.resolve(scaleModes);
        return Promise.resolve({});
      });
      render(<TransportBar />);
      await vi.waitFor(() => {
        expect(screen.getByText("C Major")).toBeInTheDocument();
      });
      fireEvent.click(screen.getByTitle("Project key and scale"));
      const modeSelect = screen.getByLabelText("Scale mode");
      await user.selectOptions(modeSelect, "1");
      expect(mockRpc.call).toHaveBeenCalledWith("project.setScaleMode", { mode: 1 });
    });

    it("click outside closes popover", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Project key and scale"));
      expect(screen.getByLabelText("Key root")).toBeInTheDocument();
      fireEvent.mouseDown(document.body);
      expect(screen.queryByLabelText("Key root")).not.toBeInTheDocument();
    });

    it("Escape closes popover", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Project key and scale"));
      expect(screen.getByLabelText("Key root")).toBeInTheDocument();
      await user.keyboard("{Escape}");
      expect(screen.queryByLabelText("Key root")).not.toBeInTheDocument();
    });
  });

  describe("store state transitions", () => {
    it("play button has active class when isPlaying", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), isPlaying: true } });
      render(<TransportBar />);
      const playBtn = screen.getByTitle("Pause");
      expect(playBtn.className).toContain("active");
    });

    it("record button has recording class when isRecording", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), isRecording: true } });
      render(<TransportBar />);
      const recBtn = screen.getByTitle("Record");
      expect(recBtn.className).toContain("recording");
    });

    it("loop button has active class when isLooping", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), isLooping: true } });
      render(<TransportBar />);
      const loopBtn = screen.getByTitle("Toggle Loop");
      expect(loopBtn.className).toContain("active");
    });

    it("punch button has active class when punchEnabled", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), punchEnabled: true } });
      render(<TransportBar />);
      const punchBtn = screen.getByTitle("Punch In/Out (uses loop region)");
      expect(punchBtn.className).toContain("active");
    });

    it("metronome button has active class when metronomeEnabled", () => {
      useTransportExtrasStore.setState({ metronomeEnabled: true });
      render(<TransportBar />);
      const metBtn = screen.getByTitle("Metronome");
      expect(metBtn.className).toContain("active");
    });

    it("follow button has active class when followPlayhead", () => {
      useTransportExtrasStore.setState({ followPlayhead: true });
      render(<TransportBar />);
      const followBtn = screen.getByTitle("Follow Playhead");
      expect(followBtn.className).toContain("active");
    });

    it("dirty indicator appears when isDirty is true", () => {
      useProjectStore.setState({ isDirty: true });
      render(<TransportBar />);
      expect(screen.getByTitle("Project has unsaved changes")).toBeInTheDocument();
    });

    it("dirty indicator hidden when isDirty is false", () => {
      useProjectStore.setState({ isDirty: false });
      render(<TransportBar />);
      expect(screen.queryByTitle("Project has unsaved changes")).not.toBeInTheDocument();
    });
  });

  describe("record arm toggles", () => {
    it("CC arm toggle calls project.setCcRecordArmed", async () => {
      render(<TransportBar />);
      const ccBtn = screen.getByTitle("Record MIDI CC automation");
      fireEvent.click(ccBtn);
      expect(mockRpc.call).toHaveBeenCalledWith("project.setCcRecordArmed", { armed: true });
      expect(ccBtn.className).toContain("recording");
    });

    it("MIDI arm toggle calls project.setMidiNoteRecordArmed", async () => {
      render(<TransportBar />);
      const miBtn = screen.getByTitle("Record MIDI notes (armed tracks)");
      fireEvent.click(miBtn);
      expect(mockRpc.call).toHaveBeenCalledWith("project.setMidiNoteRecordArmed", { armed: true });
      expect(miBtn.className).toContain("recording");
    });
  });

  describe("snap controls", () => {
    it("snap toggle sets snapEnabled", () => {
      render(<TransportBar />);
      const snapBtn = screen.getByTitle("Toggle Snap");
      fireEvent.click(snapBtn);
      expect(useUiStore.getState().snapEnabled).toBe(true);
    });

    it("snap division select changes snapDivision", async () => {
      const user = userEvent.setup();
      render(<TransportBar />);
      const select = screen.getByTitle("Toggle Snap").parentElement!.querySelector("select")!;
      await user.selectOptions(select, "3");
      expect(useUiStore.getState().snapDivision).toBe(3);
    });
  });

  describe("metronome and follow toggles", () => {
    it("metronome toggle calls project.setMetronomeEnabled and updates extras store", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Metronome"));
      expect(mockRpc.call).toHaveBeenCalledWith("project.setMetronomeEnabled", { enabled: true });
      expect(useTransportExtrasStore.getState().metronomeEnabled).toBe(true);
    });

    it("follow toggle updates extras store without RPC", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Follow Playhead"));
      expect(useTransportExtrasStore.getState().followPlayhead).toBe(true);
    });
  });

  describe("browser and view toggles", () => {
    it("browser toggle calls toggleVisible", () => {
      render(<TransportBar />);
      const btn = screen.getByTitle("Toggle File Browser (Ctrl+B)");
      fireEvent.click(btn);
      expect(useBrowserStore.getState().visible).toBe(true);
    });

    it("view mode toggle switches between session and arrange", () => {
      render(<TransportBar />);
      const btn = screen.getByTitle("Toggle Session/Arrangement View (Tab)");
      expect(btn.textContent).toBe("Arr");
      fireEvent.click(btn);
      expect(useUiStore.getState().viewMode).toBe("session");
      expect(btn.textContent).toBe("Sess");
    });
  });

  describe("dialog openers", () => {
    it("opens Plugin Manager dialog", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Plugin Manager"));
      expect(screen.getByTestId("plugin-manager")).toBeInTheDocument();
    });

    it("opens Preferences dialog", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Preferences"));
      expect(screen.getByTestId("prefs-dialog")).toBeInTheDocument();
    });

    it("opens Phrase Generator dialog", () => {
      render(<TransportBar />);
      fireEvent.click(screen.getByTitle("Phrase Generator (Ctrl+Shift+G)"));
      expect(screen.getByTestId("phrase-gen")).toBeInTheDocument();
    });
  });

  describe("display formatting", () => {
    it("formats time as m:ss", () => {
      useTransportStore.setState({ transport: { ...baseTransport(), currentTimeSeconds: 65 } });
      render(<TransportBar />);
      expect(screen.getByText("1:05")).toBeInTheDocument();
    });

    it("displays bar.beat position", () => {
      useTransportStore.setState({
        transport: { ...baseTransport(), currentTimeSeconds: 2, bpm: 120, timeSigNumerator: 4 },
      });
      render(<TransportBar />);
      // 2s at 120bpm = 4 beats, bar = floor(4/4)+1 = 2, beat = floor(4%4)+1 = 1
      expect(screen.getByText("2.1")).toBeInTheDocument();
    });
  });
});
