import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import PresetBrowser from "./PresetBrowser";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

vi.mock("../store/uiStore", () => ({
  useUiStore: Object.assign(
    (selector: any) => selector({ selectedTrackIndex: 0 }),
    { getState: vi.fn(() => ({ selectedTrackIndex: 0 })) }
  ),
}));

import { rpc } from "../rpc";

describe("PresetBrowser", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("renders search input", () => {
    render(<PresetBrowser />);
    expect(screen.getByPlaceholderText("Search presets...")).toBeTruthy();
  });

  it("calls searchPresets RPC on typing", async () => {
    (rpc.call as any).mockResolvedValue([
      { pluginId: "vst3:Serum", pluginName: "Serum", presetIndex: 0, presetName: "Init" },
    ]);
    render(<PresetBrowser />);
    const input = screen.getByPlaceholderText("Search presets...");
    fireEvent.change(input, { target: { value: "init" } });
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("plugin.searchPresets", { query: "init", limit: 100 });
    });
  });

  it("displays search results", async () => {
    (rpc.call as any).mockResolvedValue([
      { pluginId: "vst3:Serum", pluginName: "Serum", presetIndex: 0, presetName: "Init Preset" },
      { pluginId: "vst3:Serum", pluginName: "Serum", presetIndex: 5, presetName: "Warm Pad" },
    ]);
    render(<PresetBrowser />);
    fireEvent.change(screen.getByPlaceholderText("Search presets..."), { target: { value: "pad" } });
    await waitFor(() => {
      expect(screen.getByText("Init Preset")).toBeTruthy();
      expect(screen.getByText("Warm Pad")).toBeTruthy();
    });
  });

  it("calls setCurrentProgram on load click", async () => {
    (rpc.call as any)
      .mockResolvedValueOnce([
        { pluginId: "vst3:Serum", pluginName: "Serum", presetIndex: 3, presetName: "Nice Sound" },
      ])
      .mockResolvedValueOnce([
        { slotIndex: 0, fxType: "plugin", pluginId: "vst3:Serum", pluginName: "Serum", bypassed: false, paramCount: 0, pluginFormat: "VST3" },
      ])
      .mockResolvedValueOnce(null);
    render(<PresetBrowser />);
    fireEvent.change(screen.getByPlaceholderText("Search presets..."), { target: { value: "nice" } });
    await waitFor(() => expect(screen.getByText("Nice Sound")).toBeTruthy());
    fireEvent.click(screen.getByText("Nice Sound"));
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("pluginParam.setCurrentProgram", {
        trackIndex: 0,
        pluginID: "vst3:Serum",
        programIndex: 3,
      });
    });
  });
});
