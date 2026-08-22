import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import PresetBrowser from "./PresetBrowser";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

import { rpc } from "../rpc";

const mockPatterns = [
  { id: "factory/trap/dark-drill", name: "Dark Drill", style: "DrillBass", category: "trap", tags: ["808"], source: "factory" },
  { id: "user/jazz/my-bass", name: "My Bass", style: "WalkingBass", category: "jazz", tags: ["bass"], source: "user" },
  { id: "factory/ambient/evolving", name: "Evolving Pad", style: "EvolvingTexture", category: "ambient", tags: [], source: "factory" },
];

describe("PresetBrowser", () => {
  const mockOnLoad = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    (rpc.call as any).mockResolvedValue(mockPatterns);
  });

  it("renders search input", async () => {
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => expect(screen.getByPlaceholderText("Search...")).toBeTruthy());
  });

  it("loads patterns on mount", async () => {
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("composition.listPatterns", {});
    });
  });

  it("displays patterns grouped by category", async () => {
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => {
      expect(screen.getByText("Dark Drill")).toBeTruthy();
      expect(screen.getByText("My Bass")).toBeTruthy();
      expect(screen.getByText("Evolving Pad")).toBeTruthy();
    });
  });

  it("filters patterns by search", async () => {
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => expect(screen.getByText("Dark Drill")).toBeTruthy());
    
    fireEvent.change(screen.getByPlaceholderText("Search..."), { target: { value: "drill" } });
    await waitFor(() => {
      expect(screen.getByText("Dark Drill")).toBeTruthy();
      expect(screen.queryByText("Evolving Pad")).toBeNull();
    });
  });

  it("calls loadPattern on preset click", async () => {
    (rpc.call as any)
      .mockResolvedValueOnce(mockPatterns)
      .mockResolvedValueOnce({ name: "Dark Drill", style: "DrillBass", params: {}, styleParams: {} });
    
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => expect(screen.getByText("Dark Drill")).toBeTruthy());
    
    fireEvent.click(screen.getByText("Dark Drill"));
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("composition.loadPattern", { id: "factory/trap/dark-drill" });
      expect(mockOnLoad).toHaveBeenCalledWith({
        name: "Dark Drill",
        style: "DrillBass",
        params: {},
        styleParams: {},
      });
    });
  });

  it("shows delete button only for user presets", async () => {
    render(<PresetBrowser onLoadPreset={mockOnLoad} currentStyle="" />);
    await waitFor(() => expect(screen.getByText("Dark Drill")).toBeTruthy());
    
    // User preset should have delete button
    const myBassItem = screen.getByText("My Bass").closest(".preset-item");
    expect(myBassItem?.querySelector(".preset-delete")).toBeTruthy();
    
    // Factory preset should NOT have delete button
    const darkDrillItem = screen.getByText("Dark Drill").closest(".preset-item");
    expect(darkDrillItem?.querySelector(".preset-delete")).toBeNull();
  });
});
