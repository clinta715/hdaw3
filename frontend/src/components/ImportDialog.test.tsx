import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import ImportDialog from "./ImportDialog";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function seedStores(overrides?: {
  tracks?: Array<{ index: number; name: string }>;
  transport?: Partial<ReturnType<typeof useTransportStore.getState>["transport"]>;
}) {
  useTransportStore.setState({
    transport: {
      bpm: 120,
      isPlaying: false,
      isLooping: false,
      loopStart: 0,
      loopEnd: 4,
      isRecording: false,
      currentTimeSeconds: 2,
      sampleRate: 44100,
      timeSigNumerator: 4,
      timeSigDenominator: 4,
      ...overrides?.transport,
    },
  });
  useProjectStore.setState({
    snapshot: {
      name: "Test",
      tracks: overrides?.tracks ?? [],
      clips: [],
      masterGain: 1,
      scaleRoot: 0,
      scaleMode: 0,
    } as any,
  });
}

describe("ImportDialog", () => {
  const onClose = vi.fn();
  const onImport = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    seedStores();
    window.prompt = vi.fn();
  });

  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
  });

  describe("rendering", () => {
    it("shows 'Import Audio' title for audio mode", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      expect(screen.getByText("Import Audio")).toBeInTheDocument();
    });

    it("shows 'Import MIDI' title for midi mode", () => {
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      expect(screen.getByText("Import MIDI")).toBeInTheDocument();
    });

    it("shows file path input with audio placeholder", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const input = screen.getByPlaceholderText("path/to/audio.wav") as HTMLInputElement;
      expect(input).toBeInTheDocument();
      expect(input.value).toBe("");
    });

    it("shows file path input with midi placeholder", () => {
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      expect(screen.getByPlaceholderText("path/to/file.mid")).toBeInTheDocument();
    });

    it("shows track selector defaulting to 'New Track'", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const select = screen.getByDisplayValue("New Track") as HTMLSelectElement;
      expect(select).toBeInTheDocument();
      expect(select.value).toBe("new");
    });

    it("shows Browse button", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      expect(screen.getByRole("button", { name: "Browse" })).toBeInTheDocument();
    });

    it("shows Import and Cancel buttons", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      expect(screen.getByRole("button", { name: "Import" })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "Cancel" })).toBeInTheDocument();
    });
  });

  describe("track selector", () => {
    it("lists existing tracks from the project", () => {
      seedStores({
        tracks: [
          { index: 0, name: "Drums" },
          { index: 1, name: "Bass" },
        ],
      });
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      expect(screen.getByRole("option", { name: "New Track" })).toBeInTheDocument();
      expect(screen.getByRole("option", { name: "0: Drums" })).toBeInTheDocument();
      expect(screen.getByRole("option", { name: "1: Bass" })).toBeInTheDocument();
    });

    it("can select an existing track", () => {
      seedStores({
        tracks: [{ index: 0, name: "Drums" }],
      });
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const select = screen.getByRole("combobox") as HTMLSelectElement;
      fireEvent.change(select, { target: { value: "0" } });
      expect(select.value).toBe("0");
    });
  });

  describe("file browsing", () => {
    it("Browse button opens prompt and sets path for audio", () => {
      vi.mocked(window.prompt).mockReturnValue("C:/music/loop.wav");
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.click(screen.getByRole("button", { name: "Browse" }));
      expect(window.prompt).toHaveBeenCalledWith("Enter audio file path:");
      const input = screen.getByPlaceholderText("path/to/audio.wav") as HTMLInputElement;
      expect(input.value).toBe("C:/music/loop.wav");
    });

    it("Browse button opens prompt and sets path for midi", () => {
      vi.mocked(window.prompt).mockReturnValue("C:/music/melody.mid");
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      fireEvent.click(screen.getByRole("button", { name: "Browse" }));
      expect(window.prompt).toHaveBeenCalledWith("Enter MIDI file path:");
      const input = screen.getByPlaceholderText("path/to/file.mid") as HTMLInputElement;
      expect(input.value).toBe("C:/music/melody.mid");
    });

    it("canceling prompt does not change the path", () => {
      vi.mocked(window.prompt).mockReturnValue(null);
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.click(screen.getByRole("button", { name: "Browse" }));
      const input = screen.getByPlaceholderText("path/to/audio.wav") as HTMLInputElement;
      expect(input.value).toBe("");
    });

    it("typing directly into the file input works", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const input = screen.getByPlaceholderText("path/to/audio.wav") as HTMLInputElement;
      fireEvent.change(input, { target: { value: "/test/audio.wav" } });
      expect(input.value).toBe("/test/audio.wav");
    });
  });

  describe("import flow — audio", () => {
    it("clicking Import calls project.addAudioClip with correct params", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const input = screen.getByPlaceholderText("path/to/audio.wav");
      fireEvent.change(input, { target: { value: "C:/songs/beat.wav" } });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("project.addAudioClip", {
          trackIndex: 0,
          start: expect.closeTo(4, 1),
          duration: 4,
          sourceFile: "C:/songs/beat.wav",
          name: "beat.wav",
        });
      });
    });

    it("calls onImport and onClose after successful import", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/audio.wav"), {
        target: { value: "/audio/test.wav" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(onImport).toHaveBeenCalledTimes(1);
        expect(onClose).toHaveBeenCalledTimes(1);
      });
    });

    it("uses selected track index for existing track", async () => {
      mockedCall.mockResolvedValue({});
      seedStores({
        tracks: [
          { index: 0, name: "Drums" },
          { index: 1, name: "Bass" },
        ],
      });
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/audio.wav"), {
        target: { value: "/audio/kick.wav" },
      });
      fireEvent.change(screen.getByRole("combobox"), { target: { value: "1" } });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith(
          "project.addAudioClip",
          expect.objectContaining({ trackIndex: 1 })
        );
      });
    });

    it("does nothing if file path is empty", async () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.click(screen.getByRole("button", { name: "Import" }));
      await waitFor(() => {
        expect(mockedCall).not.toHaveBeenCalled();
        expect(onClose).not.toHaveBeenCalled();
      });
    });

    it("does nothing if file path is whitespace only", async () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      const input = screen.getByPlaceholderText("path/to/audio.wav");
      fireEvent.change(input, { target: { value: "   " } });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).not.toHaveBeenCalled();
      });
    });

    it("still calls onImport and onClose even if RPC rejects", async () => {
      mockedCall.mockRejectedValue(new Error("fail"));
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/audio.wav"), {
        target: { value: "/audio/err.wav" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(onImport).toHaveBeenCalled();
        expect(onClose).toHaveBeenCalled();
      });
    });

    it("extracts filename from Windows-style path", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/audio.wav"), {
        target: { value: "D:\\music\\project\\loop.wav" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith(
          "project.addAudioClip",
          expect.objectContaining({ name: "loop.wav" })
        );
      });
    });

    it("extracts filename from Unix-style path", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/audio.wav"), {
        target: { value: "/home/user/audio/beat.wav" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith(
          "project.addAudioClip",
          expect.objectContaining({ name: "beat.wav" })
        );
      });
    });
  });

  describe("import flow — midi", () => {
    it("clicking Import calls project.importMidiFile with correct params", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/file.mid"), {
        target: { value: "/midi/melody.mid" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("project.importMidiFile", {
          filePath: "/midi/melody.mid",
          trackIndex: -1,
        });
      });
    });

    it("passes selected track index for existing track", async () => {
      mockedCall.mockResolvedValue({});
      seedStores({
        tracks: [{ index: 2, name: "Synth" }],
      });
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/file.mid"), {
        target: { value: "/midi/notes.mid" },
      });
      fireEvent.change(screen.getByRole("combobox"), { target: { value: "2" } });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("project.importMidiFile", {
          filePath: "/midi/notes.mid",
          trackIndex: 2,
        });
      });
    });

    it("calls onImport and onClose after MIDI import", async () => {
      mockedCall.mockResolvedValue({});
      render(<ImportDialog mode="midi" onClose={onClose} onImport={onImport} />);
      fireEvent.change(screen.getByPlaceholderText("path/to/file.mid"), {
        target: { value: "/midi/song.mid" },
      });

      fireEvent.click(screen.getByRole("button", { name: "Import" }));

      await waitFor(() => {
        expect(onImport).toHaveBeenCalledTimes(1);
        expect(onClose).toHaveBeenCalledTimes(1);
      });
    });
  });

  describe("cancel and close", () => {
    it("clicking Cancel calls onClose", () => {
      render(<ImportDialog mode="audio" onClose={onClose} onImport={onImport} />);
      fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
      expect(onClose).toHaveBeenCalledTimes(1);
    });

    it("clicking overlay calls onClose", () => {
      const { container } = render(
        <ImportDialog mode="audio" onClose={onClose} onImport={onImport} />
      );
      const overlay = container.querySelector(".id-overlay")!;
      fireEvent.click(overlay);
      expect(onClose).toHaveBeenCalledTimes(1);
    });

    it("clicking inside dialog does not close", () => {
      const { container } = render(
        <ImportDialog mode="audio" onClose={onClose} onImport={onImport} />
      );
      const dialog = container.querySelector(".id-dialog")!;
      fireEvent.click(dialog);
      expect(onClose).not.toHaveBeenCalled();
    });
  });
});
