import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor, act } from "@testing-library/react";
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
    useAutomationStore.setState({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, loading: false, error: null, selectedPointTimes: new Map(), pinnedLaneParamID: null, lastClickedParamID: null });
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

  it("pin button appears when there is a primary lane", async () => {
    const rpc = mockRpc({ lanes: [mkLane()], params: [] });
    useAutomationStore.setState({ lanes: [mkLane()], activeTrackIndex: 0 });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("Pin")).toBeInTheDocument();
    });
  });

  it("pin button toggles pinnedLaneParamID", async () => {
    const rpc = mockRpc({ lanes: [mkLane()], params: [] });
    useAutomationStore.setState({ lanes: [mkLane()], activeTrackIndex: 0, pinnedLaneParamID: null });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("Pin")).toBeInTheDocument();
    });
    expect(useAutomationStore.getState().pinnedLaneParamID).toBeNull();
    fireEvent.click(screen.getByText("Pin"));
    expect(useAutomationStore.getState().pinnedLaneParamID).toBe(100);
    fireEvent.click(screen.getByText("Pin"));
    expect(useAutomationStore.getState().pinnedLaneParamID).toBeNull();
  });

  it("when unpinned, joker creates a lane for new paramID", async () => {
    const volumeLane = mkLane({ name: "Volume", paramID: 100 });
    const rpc = mockRpc({ lanes: [volumeLane], params: [] });
    useAutomationStore.setState({ lanes: [volumeLane], activeTrackIndex: 0, pinnedLaneParamID: null, lastClickedParamID: null });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("Pin")).toBeInTheDocument();
    });
    // Simulate clicking a pan slider (paramID 2)
    useAutomationStore.getState().setLastClickedParamID(2);
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.addAutomationLane", {
        trackIndex: 0,
        laneName: "Pan",
        paramID: 2,
      });
    });
  });

  it("when pinned, changing lastClickedParamID does NOT trigger lane creation", async () => {
    const volumeLane = mkLane({ name: "Volume", paramID: 100 });
    const rpc = mockRpc({ lanes: [volumeLane], params: [] });
    useAutomationStore.setState({ lanes: [volumeLane], activeTrackIndex: 0, pinnedLaneParamID: 100, lastClickedParamID: null });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("Pin")).toBeInTheDocument();
    });
    const callCount = (rpc.call as ReturnType<typeof vi.fn>).mock.calls.length;
    useAutomationStore.getState().setLastClickedParamID(2);
    // Wait a tick to let any effect fire
    await new Promise((r) => setTimeout(r, 50));
    expect((rpc.call as ReturnType<typeof vi.fn>).mock.calls.length).toBe(callCount);
  });

  it("when unpinned, joker reorders existing lane to primary without rpc", async () => {
    const volumeLane = mkLane({ laneIndex: 0, name: "Volume", paramID: 100 });
    const panLane = mkLane({ laneIndex: 1, name: "Pan", paramID: 2 });
    const rpc = mockRpc({ lanes: [volumeLane, panLane], params: [] });
    useAutomationStore.setState({ lanes: [volumeLane, panLane], activeTrackIndex: 0, pinnedLaneParamID: null, lastClickedParamID: null });
    render(<AutomationPanel rpc={rpc} />);
    await waitFor(() => {
      expect(screen.getByText("Pin")).toBeInTheDocument();
    });
    const callCount = (rpc.call as ReturnType<typeof vi.fn>).mock.calls.length;
    useAutomationStore.getState().setLastClickedParamID(2);
    await waitFor(() => {
      const st = useAutomationStore.getState();
      expect(st.lanes[0].paramID).toBe(2);
    });
    expect((rpc.call as ReturnType<typeof vi.fn>).mock.calls.length).toBe(callCount);
  });

  it("joker effect does not cause re-render cascade when lastClickedParamID changes", async () => {
    useAutomationStore.setState({
      lanes: [mkLane({ laneIndex: 0, paramID: 1 })],
      activeTrackIndex: 0,
      pinnedLaneParamID: null,
      lastClickedParamID: null,
    });
    const rpc = mockRpc({ lanes: [mkLane({ laneIndex: 0, paramID: 1 })], params: [] });

    let renderCount = 0;
    function Counter({ children }: { children: React.ReactNode }) {
      renderCount++;
      return <>{children}</>;
    }

    render(<Counter><AutomationPanel rpc={rpc} /></Counter>);
    const baseline = renderCount;
    renderCount = 0;

    // Trigger the joker effect — this used to cause an infinite re-render loop
    // because the effect depended on lastClickedParamID from useShallow, and
    // the effect's store mutation (lanes reorder) triggered useShallow to see
    // a new object, re-rendering the component, which re-fired the effect.
    act(() => {
      useAutomationStore.getState().setLastClickedParamID(2);
    });

    // Wait for async effects to settle
    await new Promise((r) => setTimeout(r, 100));

    // Should be a small number of additional renders, not a cascade.
    // The fixed code uses subscribe() so the effect doesn't cause re-renders
    // via the dependency array at all.
    expect(renderCount).toBeLessThan(5);
  });
});
