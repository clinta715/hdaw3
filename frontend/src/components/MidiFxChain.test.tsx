import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act, waitFor } from "@testing-library/react";
import MidiFxChain from "./MidiFxChain";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { useNotifyStore } from "../store/notifyStore";
import type { MidiFxSlotSnapshot } from "../rpc/types";

// Same RPC mock pattern as WaveformCanvas.test.tsx.
vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

// Helper to flush the read.getMidiFxSlots effect (.then chain).
async function flushRead() {
  await act(async () => {
    // Let pending microtasks (promise resolutions) drain.
    await Promise.resolve();
    await Promise.resolve();
  });
}

// Wait for a specific RPC method to have been invoked at least n times.
async function waitForCall(method: string, n = 1) {
  await waitFor(() => {
    const count = mockedCall.mock.calls.filter((c) => c[0] === method).length;
    expect(count).toBeGreaterThanOrEqual(n);
  });
}

const EMPTY: MidiFxSlotSnapshot[] = [];
const TWO_SLOTS: MidiFxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "arpeggiator", bypassed: false },
  { slotIndex: 1, fxType: "velocity", bypassed: true },
];

describe("MidiFxChain", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    useUiStore.setState({ selectedTrackIndex: null });
    useNotifyStore.getState().clear();
  });

  afterEach(() => {
    cleanup();
  });

  // --- Render / read path ---

  it("shows the empty state and does not call rpc when no track is selected", () => {
    render(<MidiFxChain />);
    expect(screen.getByText("Select a track to edit MIDI FX")).toBeInTheDocument();
    expect(mockedCall).not.toHaveBeenCalled();
  });

  it("renders header, dropdown options, and empty message when the track has no slots", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(EMPTY);
    render(<MidiFxChain />);
    await flushRead();

    expect(screen.getByText("MIDI FX — Track 2")).toBeInTheDocument();
    expect(screen.getByText("No MIDI FX on this track")).toBeInTheDocument();
    // The "+ Add" placeholder and all five FX options are present.
    expect(screen.getByText("+ Add MIDI FX…")).toBeInTheDocument();
    expect(screen.getByText("Arpeggiator")).toBeInTheDocument();
    expect(screen.getByText("Velocity")).toBeInTheDocument();
    expect(screen.getByText("Chord")).toBeInTheDocument();
    expect(screen.getByText("Scale Quantize")).toBeInTheDocument();
    expect(screen.getByText("Note Length")).toBeInTheDocument();
    // Initial read of slots for the selected track.
    expect(mockedCall).toHaveBeenCalledWith("read.getMidiFxSlots", { trackIndex: 2 });
  });

  it("renders a row per slot with index, type label, and bypassed class on bypassed rows", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(TWO_SLOTS);
    render(<MidiFxChain />);
    await flushRead();

    const rows = screen.getAllByText((_, el) => el?.classList.contains("mfx-slot") ?? false);
    expect(rows).toHaveLength(2);

    // Slot 0 — active arpeggiator.
    expect(rows[0].querySelector(".mfx-slot-index")?.textContent).toBe("0");
    expect(rows[0].querySelector(".mfx-slot-type")?.textContent).toBe("Arpeggiator");
    expect(rows[0].classList.contains("mfx-slot--bypassed")).toBe(false);

    // Slot 1 — bypassed velocity.
    expect(rows[1].querySelector(".mfx-slot-index")?.textContent).toBe("1");
    expect(rows[1].querySelector(".mfx-slot-type")?.textContent).toBe("Velocity");
    expect(rows[1].classList.contains("mfx-slot--bypassed")).toBe(true);
  });

  it("treats a non-array RPC response as empty (defensive Array.isArray guard)", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(null);
    render(<MidiFxChain />);
    await flushRead();

    expect(screen.getByText("No MIDI FX on this track")).toBeInTheDocument();
  });

  // --- Interactions -> RPC contracts ---

  it("changing the dropdown to chord calls addMidiFxSlot then refreshes", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(EMPTY);
    render(<MidiFxChain />);
    await flushRead();

    fireEvent.change(screen.getByRole("combobox"), { target: { value: "chord" } });
    await waitForCall("project.addMidiFxSlot");
    await flushRead();

    expect(mockedCall).toHaveBeenCalledWith("project.addMidiFxSlot", { trackIndex: 2, type: "chord" });
    // Refresh: a second read.getMidiFxSlots beyond the initial one.
    const reads = mockedCall.mock.calls.filter((c) => c[0] === "read.getMidiFxSlots").length;
    expect(reads).toBeGreaterThanOrEqual(2);
  });

  it("clicking Del on slot 0 calls removeMidiFxSlot with the slot index", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(TWO_SLOTS);
    render(<MidiFxChain />);
    await flushRead();

    const delButtons = screen.getAllByTitle("Remove");
    fireEvent.click(delButtons[0]);
    await waitForCall("project.removeMidiFxSlot");

    expect(mockedCall).toHaveBeenCalledWith("project.removeMidiFxSlot", {
      trackIndex: 2,
      slotIndex: 0,
    });
  });

  it("clicking Byp on an active slot sends bypassed: true", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(TWO_SLOTS);
    render(<MidiFxChain />);
    await flushRead();

    const bypButtons = screen.getAllByTitle("Toggle bypass");
    // Slot 0 is active in TWO_SLOTS -> clicking should bypass.
    fireEvent.click(bypButtons[0]);
    await waitForCall("project.setMidiFxSlotBypassed");

    expect(mockedCall).toHaveBeenCalledWith("project.setMidiFxSlotBypassed", {
      trackIndex: 2,
      slotIndex: 0,
      bypassed: true,
    });
  });

  it("clicking Byp on a bypassed slot sends bypassed: false", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(TWO_SLOTS);
    render(<MidiFxChain />);
    await flushRead();

    const bypButtons = screen.getAllByTitle("Toggle bypass");
    // Slot 1 is bypassed in TWO_SLOTS -> clicking should un-bypass.
    fireEvent.click(bypButtons[1]);
    await waitForCall("project.setMidiFxSlotBypassed");

    expect(mockedCall).toHaveBeenCalledWith("project.setMidiFxSlotBypassed", {
      trackIndex: 2,
      slotIndex: 1,
      bypassed: false,
    });
  });

  // --- Reactivity / error path ---

  it("re-fetches with the new track index when selectedTrackIndex changes", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    mockedCall.mockResolvedValue(EMPTY);
    render(<MidiFxChain />);
    await flushRead();
    expect(mockedCall).toHaveBeenLastCalledWith("read.getMidiFxSlots", { trackIndex: 2 });

    // Switch the selected track; the effect depends on selectedTrackIndex.
    act(() => {
      useUiStore.setState({ selectedTrackIndex: 3 });
    });
    await flushRead();

    expect(mockedCall).toHaveBeenLastCalledWith("read.getMidiFxSlots", { trackIndex: 3 });
  });

  it("pushes an error toast when addMidiFxSlot rejects (reportRpcError catch path)", async () => {
    useUiStore.setState({ selectedTrackIndex: 2 });
    // Initial read resolves; the add RPC rejects.
    mockedCall.mockImplementation((method: string) => {
      if (method === "project.addMidiFxSlot") return Promise.reject(new Error("boom"));
      return Promise.resolve(EMPTY);
    });
    render(<MidiFxChain />);
    await flushRead();

    fireEvent.change(screen.getByRole("combobox"), { target: { value: "chord" } });
    await waitForCall("project.addMidiFxSlot");

    // reportRpcError surfaces the failure as an error toast naming the method.
    const { toasts } = useNotifyStore.getState();
    expect(toasts.length).toBe(1);
    expect(toasts[0].level).toBe("error");
    expect(toasts[0].message).toContain("project.addMidiFxSlot");
    expect(toasts[0].message).toContain("boom");
  });
});
