import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import AutomationPanel from "./AutomationPanel";
import { useUiStore } from "../store/uiStore";
import { useAutomationStore } from "../store/automationStore";
import type { RpcClient } from "../rpc/client";

// AutomationLaneCanvas uses ResizeObserver which is not in jsdom
beforeEach(() => {
  if (typeof globalThis.ResizeObserver === "undefined") {
    (globalThis as any).ResizeObserver = class {
      observe() {}
      unobserve() {}
      disconnect() {}
    };
  }
});

function mockRpc(overrides: Record<string, unknown> = {}): RpcClient {
  const call = vi.fn(async (method: string) => {
    if (method === "read.getAutomationLanes") return overrides.lanes ?? [];
    if (method === "read.getAutomationPoints") return overrides.points ?? [];
    if (method === "read.getAutomatableParams") return overrides.params ?? [];
    return null;
  });
  return { call } as unknown as RpcClient;
}

function mkLane(over: Partial<{ laneIndex: number; name: string; paramID: number; enabled: boolean; mode: string }> = {}) {
  return { laneIndex: 0, name: "Volume", paramID: 100, enabled: true, mode: "read", ...over };
}

describe("AutomationPanel", () => {
  beforeEach(() => {
    useUiStore.setState({ selectedTrackIndex: 0, selectedClipIds: new Set() });
    useAutomationStore.setState({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, loading: false, error: null, selectedPointTimes: new Map() });
  });

  afterEach(() => cleanup());

  it("shows empty state when no track is selected", () => {
    useUiStore.setState({ selectedTrackIndex: null });
    const rpc = mockRpc();
    render(<AutomationPanel rpc={rpc} />);
    expect(screen.getByText(/Select a clip to edit automation lanes/)).toBeInTheDocument();
  });

  it("shows loading state while fetching", () => {
    useAutomationStore.setState({ loading: true });
    const rpc = mockRpc();
    render(<AutomationPanel rpc={rpc} />);
    expect(screen.getByText("Loading...")).toBeInTheDocument();
  });

  it("shows 'No automation lanes' when lanes are empty", async () => {
    const rpc = mockRpc({ lanes: [], params: [] });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText(/No automation lanes/)).toBeInTheDocument();
    });
  });

  it("renders lane rows with mode buttons", async () => {
    const rpc = mockRpc({ lanes: [mkLane()], params: [] });
    useAutomationStore.setState({ lanes: [mkLane()], activeTrackIndex: 0 });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("R")).toBeInTheDocument(); // read mode button
      expect(screen.getByText("W")).toBeInTheDocument(); // write mode button
      expect(screen.getByText("T")).toBeInTheDocument(); // touch mode button
      expect(screen.getByText("L")).toBeInTheDocument(); // latch mode button
    });
  });

  it("calls project.setAutomationMode when mode button clicked", async () => {
    const rpc = mockRpc({ lanes: [mkLane({ mode: "read" })], params: [] });
    useAutomationStore.setState({ lanes: [mkLane({ mode: "read" })], activeTrackIndex: 0 });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("W")).toBeInTheDocument();
    });
    fireEvent.click(screen.getByText("W"));
    expect(rpc.call).toHaveBeenCalledWith("project.setAutomationMode", {
      trackIndex: 0,
      laneName: "Volume",
      mode: "write",
    });
  });

  it("add lane button is disabled when no automatable params", async () => {
    const rpc = mockRpc({ lanes: [], params: [] });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      const btn = screen.getByText("+ Add Lane");
      expect(btn).toBeDisabled();
    });
  });

  it("shows param select with available params", async () => {
    const params = [{ slotIndex: 0, paramIndex: 0, name: "Gain", automatable: true }];
    const rpc = mockRpc({ lanes: [], params });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("S0 · Gain")).toBeInTheDocument();
    });
  });
});
