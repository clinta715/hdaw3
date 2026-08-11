import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import FileBrowser from "./FileBrowser";
import { useBrowserStore } from "../store/browserStore";
import { useLibraryStore } from "../store/libraryStore";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn().mockResolvedValue(null) } }));

const mockReadDir = vi.fn().mockResolvedValue([
  { name: "kick.wav", path: "/test/kick.wav", isDir: false },
  { name: "melody.mid", path: "/test/melody.mid", isDir: false },
  { name: "pad.aiff", path: "/test/pad.aiff", isDir: false },
]);

Object.defineProperty(window, "hdaw", {
  value: {
    readDirectory: mockReadDir,
    showOpenDialog: vi.fn().mockResolvedValue({ canceled: true, filePaths: [] }),
  },
  writable: true,
});

describe("FileBrowser filter chips", () => {
  beforeEach(() => {
    mockReadDir.mockClear();
    useBrowserStore.setState({
      folders: ["/test"],
      expandedPaths: new Set(["/test"]),
      visible: true,
      kindFilter: "all",
      searchQuery: "",
    });
  });

  afterEach(() => cleanup());

  it("renders filter chip buttons", () => {
    render(<FileBrowser />);
    expect(screen.getByText("All")).toBeInTheDocument();
    expect(screen.getByText("Samples")).toBeInTheDocument();
    expect(screen.getByText("MIDI")).toBeInTheDocument();
    expect(screen.getByText("Devices")).toBeInTheDocument();
    expect(screen.getByText("Presets")).toBeInTheDocument();
    expect(screen.getByText("Clips")).toBeInTheDocument();
  });

  it("highlights the active filter chip", () => {
    useBrowserStore.setState({ kindFilter: "samples" });
    render(<FileBrowser />);
    const samplesChip = screen.getByText("Samples");
    expect(samplesChip.className).toContain("fb-chip--active");
    const allChip = screen.getByText("All");
    expect(allChip.className).not.toContain("fb-chip--active");
  });

  it("clicking a chip toggles the filter", () => {
    render(<FileBrowser />);
    const midiChip = screen.getByText("MIDI");
    fireEvent.click(midiChip);
    expect(useBrowserStore.getState().kindFilter).toBe("midi");
    fireEvent.click(midiChip);
    expect(useBrowserStore.getState().kindFilter).toBe("all");
  });

  it("files are filtered by kind when a filter is active", async () => {
    useBrowserStore.setState({ kindFilter: "samples" });
    render(<FileBrowser />);
    await waitFor(() => {
      expect(screen.getByText("kick.wav")).toBeInTheDocument();
      expect(screen.getByText("pad.aiff")).toBeInTheDocument();
    });
    expect(screen.queryByText("melody.mid")).toBeNull();
  });

  it("showing all supported files when kindFilter is 'all'", async () => {
    render(<FileBrowser />);
    await waitFor(() => {
      expect(screen.getByText("kick.wav")).toBeInTheDocument();
      expect(screen.getByText("melody.mid")).toBeInTheDocument();
      expect(screen.getByText("pad.aiff")).toBeInTheDocument();
    });
  });
});

describe("FileBrowser library mode", () => {
  beforeEach(() => {
    useBrowserStore.setState({
      folders: ["/test"],
      expandedPaths: new Set(["/test"]),
      visible: true,
      kindFilter: "library",
      searchQuery: "",
    });
    useLibraryStore.setState({
      libraries: [
        { id: "lib1", name: "My Samples", path: "/audio", type: "audio", lastScan: "", fileCount: 42, autoScan: false },
      ],
      searchResults: [
        { name: "kick.wav", path: "/audio/kick.wav", size: 102400, durationSeconds: 2.5, key: "C", bpm: 120 },
        { name: "snare.wav", path: "/audio/snare.wav", size: 51200, durationSeconds: 1.2, key: "", bpm: 0 },
      ],
      scanProgress: {},
      searchQuery: "",
      loading: false,
    });
  });

  afterEach(() => cleanup());

  it("renders the Library chip", () => {
    render(<FileBrowser />);
    expect(screen.getByText("Library")).toBeInTheDocument();
  });

  it("shows LibraryView when kindFilter is library", () => {
    render(<FileBrowser />);
    expect(screen.getByText("Rescan All")).toBeInTheDocument();
    expect(screen.getByText("My Samples")).toBeInTheDocument();
  });

  it("hides the file tree in library mode", () => {
    render(<FileBrowser />);
    expect(screen.queryByText("No folders added")).toBeNull();
  });

  it("shows search results with metadata columns", () => {
    render(<FileBrowser />);
    expect(screen.getByText("kick.wav")).toBeInTheDocument();
    expect(screen.getByText("snare.wav")).toBeInTheDocument();
    expect(screen.getByText("Name")).toBeInTheDocument();
    expect(screen.getByText("Duration")).toBeInTheDocument();
    expect(screen.getByText("BPM")).toBeInTheDocument();
    expect(screen.getByText("Key")).toBeInTheDocument();
    expect(screen.getByText("Size")).toBeInTheDocument();
  });

  it("shows empty state when no libraries configured", () => {
    useLibraryStore.setState({ libraries: [], searchResults: [] });
    render(<FileBrowser />);
    expect(screen.getByText("No libraries configured. Add one in Preferences.")).toBeInTheDocument();
  });

  it("Rescan All button calls scanAll", () => {
    const rpcMock = { call: vi.fn().mockResolvedValue(null) };
    vi.mocked(rpc).call = rpcMock.call;
    render(<FileBrowser />);
    fireEvent.click(screen.getByText("Rescan All"));
    expect(rpcMock.call).toHaveBeenCalledWith("library.scan", {});
  });
});
