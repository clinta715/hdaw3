import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import PopUpBrowser from "./PopUpBrowser";
import { useBrowserStore } from "../store/browserStore";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn().mockResolvedValue(null) } }));

const mockReadDir = vi.fn().mockResolvedValue([]);
Object.defineProperty(window, "hdaw", {
  value: { readDirectory: mockReadDir },
  writable: true,
  configurable: true,
});

describe("PopUpBrowser", () => {
  const onSelect = vi.fn();
  const onClose = vi.fn();

  beforeEach(() => {
    onSelect.mockReset();
    onClose.mockReset();
    mockReadDir.mockReset();
    mockReadDir.mockResolvedValue([]);
    useBrowserStore.setState({ folders: ["/test"] });
  });

  afterEach(() => cleanup());

  it("renders the overlay when opened", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    expect(screen.getByText("Browse Files")).toBeInTheDocument();
  });

  it("renders with context-specific title for clip", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} context="clip" />);
    expect(screen.getByText("Add Clip")).toBeInTheDocument();
  });

  it("renders with context-specific title for device", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} context="device" />);
    expect(screen.getByText("Add Device")).toBeInTheDocument();
  });

  it("shows empty state when no folders configured", () => {
    useBrowserStore.setState({ folders: [] });
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    expect(screen.getByText("No files found. Add folders in the Browser panel.")).toBeInTheDocument();
  });

  it("calls onClose when Escape is pressed", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    fireEvent.keyDown(window, { key: "Escape" });
    expect(onClose).toHaveBeenCalled();
  });

  it("calls onClose when backdrop is clicked", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    const backdrop = document.querySelector(".pb-backdrop");
    expect(backdrop).not.toBeNull();
    fireEvent.mouseDown(backdrop!);
    expect(onClose).toHaveBeenCalled();
  });

  it("renders filter chips", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    expect(screen.getByText("All")).toBeInTheDocument();
    expect(screen.getByText("Samples")).toBeInTheDocument();
    expect(screen.getByText("MIDI")).toBeInTheDocument();
    expect(screen.getByText("Devices")).toBeInTheDocument();
    expect(screen.getByText("Presets")).toBeInTheDocument();
    expect(screen.getByText("Clips")).toBeInTheDocument();
  });

  it("filters chips by context - clip only shows samples and clips", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} context="clip" />);
    expect(screen.getByText("All")).toBeInTheDocument();
    expect(screen.getByText("Samples")).toBeInTheDocument();
    expect(screen.getByText("Clips")).toBeInTheDocument();
    expect(screen.queryByText("Devices")).toBeNull();
    expect(screen.queryByText("Presets")).toBeNull();
  });

  it("filters chips by context - device only shows devices and presets", () => {
    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} context="device" />);
    expect(screen.getByText("All")).toBeInTheDocument();
    expect(screen.getByText("Devices")).toBeInTheDocument();
    expect(screen.getByText("Presets")).toBeInTheDocument();
    expect(screen.queryByText("Samples")).toBeNull();
    expect(screen.queryByText("MIDI")).toBeNull();
  });

  it("displays scanned files", async () => {
    mockReadDir.mockResolvedValue([
      { name: "kick.wav", path: "/test/kick.wav", isDir: false },
      { name: "snare.aiff", path: "/test/snare.aiff", isDir: false },
    ]);

    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    expect(await screen.findByText("kick.wav")).toBeInTheDocument();
    expect(screen.getByText("snare.aiff")).toBeInTheDocument();
    expect(screen.getByText("2 items")).toBeInTheDocument();
  });

  it("selects an item on click and calls onSelect", async () => {
    mockReadDir.mockResolvedValue([
      { name: "kick.wav", path: "/test/kick.wav", isDir: false },
    ]);

    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    const item = await screen.findByText("kick.wav");
    fireEvent.click(item.closest(".pb-item")!);
    expect(onSelect).toHaveBeenCalledWith({
      path: "/test/kick.wav",
      name: "kick.wav",
      kind: "samples",
    });
  });

  it("filters items by kind when a chip is clicked", async () => {
    mockReadDir.mockResolvedValue([
      { name: "kick.wav", path: "/kick.wav", isDir: false },
      { name: "melody.mid", path: "/melody.mid", isDir: false },
    ]);

    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    await screen.findByText("kick.wav");

    fireEvent.click(screen.getByText("MIDI"));

    expect(screen.queryByText("kick.wav")).toBeNull();
    expect(screen.getByText("melody.mid")).toBeInTheDocument();
  });

  it("filters items by search query", async () => {
    mockReadDir.mockResolvedValue([
      { name: "kick.wav", path: "/kick.wav", isDir: false },
      { name: "snare.wav", path: "/snare.wav", isDir: false },
    ]);

    render(<PopUpBrowser onSelect={onSelect} onClose={onClose} />);
    await screen.findByText("kick.wav");

    const searchInput = screen.getByPlaceholderText("Search...");
    fireEvent.change(searchInput, { target: { value: "snare" } });

    expect(screen.queryByText("kick.wav")).toBeNull();
    expect(screen.getByText("snare.wav")).toBeInTheDocument();
  });
});
