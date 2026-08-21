import { render, screen, waitFor, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import PoolView from "./PoolView";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

vi.mock("../store/projectStore", () => ({
  useProjectStore: Object.assign(
    vi.fn(() => ({ snapshot: { tracks: [] } })),
    { getState: vi.fn(() => ({ snapshot: { tracks: [] } })) }
  ),
}));

vi.mock("../store/uiStore", () => ({
  useUiStore: Object.assign(
    vi.fn(() => ({ selectedTrackIndex: 0 })),
    { getState: vi.fn(() => ({ selectedTrackIndex: 0 })) }
  ),
}));

vi.mock("../store/transportStore", () => ({
  useTransportStore: Object.assign(
    vi.fn(() => ({ transport: { bpm: 120 } })),
    { getState: vi.fn(() => ({ transport: { bpm: 120 } })) }
  ),
}));

import { rpc } from "../rpc";

describe("PoolView", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders empty state when no files", async () => {
    (rpc.call as any).mockResolvedValue([]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("No audio files in project")).toBeTruthy();
    });
  });

  it("displays pool entries", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/path/drums.wav", name: "drums", usageCount: 3, duration: 120.5, sampleRate: 44100, channels: 2 },
      { sourceFile: "/path/bass.wav", name: "bass", usageCount: 1, duration: 60.0, sampleRate: 48000, channels: 1 },
    ]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("drums")).toBeTruthy();
      expect(screen.getByText("bass")).toBeTruthy();
      expect(screen.getByText("3")).toBeTruthy();
      expect(screen.getAllByText("1").length).toBeGreaterThanOrEqual(1);
    });
  });

  it("filters entries by name", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/path/drums.wav", name: "drums", usageCount: 2, duration: 60, sampleRate: 44100, channels: 2 },
      { sourceFile: "/path/bass.wav", name: "bass", usageCount: 1, duration: 30, sampleRate: 44100, channels: 1 },
    ]);
    render(<PoolView />);
    await waitFor(() => expect(screen.getByText("drums")).toBeTruthy());
    fireEvent.change(screen.getByPlaceholderText("Filter files..."), { target: { value: "bass" } });
    expect(screen.queryByText("drums")).toBeNull();
    expect(screen.getByText("bass")).toBeTruthy();
  });

  it("shows file count in footer", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/a.wav", name: "a", usageCount: 1, duration: 10, sampleRate: 44100, channels: 2 },
      { sourceFile: "/b.wav", name: "b", usageCount: 1, duration: 10, sampleRate: 44100, channels: 2 },
    ]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("2 files")).toBeTruthy();
    });
  });

  it("highlights unused entries with usageCount 0", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/scratch.wav", name: "scratch", usageCount: 0, duration: 10, sampleRate: 44100, channels: 2 },
      { sourceFile: "/used.wav", name: "used", usageCount: 3, duration: 10, sampleRate: 44100, channels: 2 },
    ]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("scratch")).toBeTruthy();
      expect(screen.getByText("used")).toBeTruthy();
    });
    const items = document.querySelectorAll(".pool-view-item");
    expect(items[0].classList.contains("pool-view-item--unused")).toBe(true);
    expect(items[1].classList.contains("pool-view-item--unused")).toBe(false);
  });

  it("shows unused badge for zero-usage entries", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/scratch.wav", name: "scratch", usageCount: 0, duration: 10, sampleRate: 44100, channels: 2 },
    ]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("scratch")).toBeTruthy();
    });
    expect(document.querySelector(".pool-unused-badge")).toBeTruthy();
  });

  it("sets draggable attribute on pool entries", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/path/drums.wav", name: "drums", usageCount: 1, duration: 60, sampleRate: 44100, channels: 2 },
    ]);
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("drums")).toBeTruthy();
    });
    const item = document.querySelector(".pool-view-item") as HTMLElement;
    expect(item.getAttribute("draggable")).toBe("true");
  });

  it("starts preview on click and calls correct RPCs", async () => {
    (rpc.call as any).mockResolvedValue([
      { sourceFile: "/path/drums.wav", name: "drums", usageCount: 1, duration: 60, sampleRate: 44100, channels: 2 },
    ]);
    (rpc.call as any).mockImplementation((method: string) => {
      if (method === "pool.list") return Promise.resolve([
        { sourceFile: "/path/drums.wav", name: "drums", usageCount: 1, duration: 60, sampleRate: 44100, channels: 2 },
      ]);
      if (method === "preview.isPlaying") return Promise.resolve(true);
      return Promise.resolve();
    });
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("drums")).toBeTruthy();
    });
    const item = document.querySelector(".pool-view-item") as HTMLElement;
    fireEvent.click(item);
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("preview.load", { filePath: "/path/drums.wav" });
      expect(rpc.call).toHaveBeenCalledWith("preview.play");
    });
  });

  it("stops preview on second click of same entry", async () => {
    (rpc.call as any).mockImplementation((method: string) => {
      if (method === "pool.list") return Promise.resolve([
        { sourceFile: "/path/drums.wav", name: "drums", usageCount: 1, duration: 60, sampleRate: 44100, channels: 2 },
      ]);
      if (method === "preview.isPlaying") return Promise.resolve(true);
      return Promise.resolve();
    });
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("drums")).toBeTruthy();
    });
    const item = document.querySelector(".pool-view-item") as HTMLElement;
    // First click: start
    fireEvent.click(item);
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("preview.play");
    });
    // Second click: stop
    fireEvent.click(item);
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("preview.stop");
    });
  });

  it("applies previewing class while preview is active", async () => {
    (rpc.call as any).mockImplementation((method: string) => {
      if (method === "pool.list") return Promise.resolve([
        { sourceFile: "/path/drums.wav", name: "drums", usageCount: 1, duration: 60, sampleRate: 44100, channels: 2 },
      ]);
      if (method === "preview.isPlaying") return Promise.resolve(true);
      return Promise.resolve();
    });
    render(<PoolView />);
    await waitFor(() => {
      expect(screen.getByText("drums")).toBeTruthy();
    });
    const item = document.querySelector(".pool-view-item") as HTMLElement;
    fireEvent.click(item);
    await waitFor(() => {
      expect(item.classList.contains("pool-view-item--previewing")).toBe(true);
    });
  });
});
