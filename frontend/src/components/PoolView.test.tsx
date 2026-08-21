import { render, screen, waitFor, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import PoolView from "./PoolView";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
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
});
