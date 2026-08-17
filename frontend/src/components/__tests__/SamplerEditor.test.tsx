import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, cleanup, act, fireEvent } from "@testing-library/react";
import SamplerEditor from "../SamplerEditor";
import { rpc } from "../../rpc";

vi.mock("../../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
    await Promise.resolve();
  });
}

vi.mock("../../store/uiStore", async (importOriginal) => {
  const actual = await importOriginal() as Record<string, unknown>;
  return {
    ...actual,
    useUiStore: (selector: (s: Record<string, unknown>) => unknown) => {
      const state = { selectedTrackIndex: 0 };
      return selector ? selector(state) : state;
    },
  };
});

function mockSamplerState(overrides: Record<string, unknown> = {}) {
  return {
    sampleFile: "",
    mode: "classic",
    rootNote: 60,
    transpose: 0,
    mono: false,
    playReverse: false,
    envelope: { attack: 0.005, hold: 0, decay: 0.1, sustain: 0.9, release: 0.1 },
    sampleStart: 0,
    sampleEnd: 1,
    glide: 0,
    hasSound: false,
    activeVoices: 0,
    ...overrides,
  };
}

async function renderSampler(state: Record<string, unknown>) {
  mockedCall
    .mockResolvedValueOnce([{ fxType: "sampler" }])
    .mockResolvedValueOnce(state);
  const utils = render(<SamplerEditor />);
  await flushRead();
  return utils;
}

describe("SamplerEditor", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(undefined);
  });

  afterEach(() => {
    cleanup();
  });

  it("renders empty state when no sampler slot found", async () => {
    mockedCall.mockResolvedValueOnce([{ fxType: "eq" }]);
    render(<SamplerEditor />);
    await flushRead();
    expect(screen.getByText(/no sampler/i)).toBeTruthy();
  });

  it("renders controls when sampler slot exists", async () => {
    await renderSampler(mockSamplerState());
    expect(screen.getByText("Mode")).toBeTruthy();
    expect(screen.getByText("Root")).toBeTruthy();
    expect(screen.getByText("Transpose")).toBeTruthy();
    expect(screen.getByText("Mono")).toBeTruthy();
    expect(screen.getByText("A")).toBeTruthy();
    expect(screen.getByText("D")).toBeTruthy();
    expect(screen.getByText("S")).toBeTruthy();
    expect(screen.getByText("R")).toBeTruthy();
  });

  it("fetches sampler state via sampler.getState", async () => {
    await renderSampler(mockSamplerState());
    expect(mockedCall).toHaveBeenCalledWith("read.getFxSlots", { trackIndex: 0 });
    expect(mockedCall).toHaveBeenCalledWith("sampler.getState", { trackIndex: 0, slotIndex: 0 });
  });

  it("changing the mode select calls sampler.setMode", async () => {
    const { container } = await renderSampler(mockSamplerState());
    const select = container.querySelector(".sampler-editor__select") as HTMLSelectElement;
    fireEvent.change(select, { target: { value: "slice" } });
    await flushRead();
    expect(mockedCall).toHaveBeenCalledWith("sampler.setMode", { trackIndex: 0, slotIndex: 0, mode: "slice" });
  });

  it("a param slider change calls sampler.setParam", async () => {
    const { container } = await renderSampler(mockSamplerState());
    const slider = container.querySelector(".sampler-editor__slider") as HTMLInputElement;
    fireEvent.change(slider, { target: { value: "1.5" } });
    await flushRead();
    expect(mockedCall).toHaveBeenCalledWith("sampler.setParam", { trackIndex: 0, slotIndex: 0, paramIndex: 7, value: 1.5 });
  });

  it("Mono checkbox calls sampler.setParam with the property form", async () => {
    await renderSampler(mockSamplerState());
    const mono = screen.getByLabelText("Mono") as HTMLInputElement;
    expect(mono).toBeTruthy();
    fireEvent.click(mono);
    await flushRead();
    expect(mockedCall).toHaveBeenCalledWith("sampler.setParam", {
      trackIndex: 0,
      slotIndex: 0,
      property: "mono",
      value: true,
    });
  });

  it("in slice mode, Detect calls sampler.detectSlices", async () => {
    const { container } = await renderSampler(mockSamplerState({
      mode: "slice",
      sliceMode: "transient",
      sliceSensitivity: 0.5,
      sliceGrid: 0.25,
      slicePoints: [0, 0.25, 0.5, 1],
    }));
    const detectBtn = Array.from(container.querySelectorAll("button"))
      .find((b) => b.textContent === "Detect");
    expect(detectBtn).toBeTruthy();
    fireEvent.click(detectBtn!);
    await flushRead();
    expect(mockedCall).toHaveBeenCalledWith("sampler.detectSlices", {
      trackIndex: 0,
      slotIndex: 0,
      sliceMode: "transient",
      sliceGrid: 0.25,
      sliceSensitivity: 0.5,
    });
  });

  it("clicking a slice item calls sampler.triggerSlice with that index", async () => {
    const { container } = await renderSampler(mockSamplerState({
      mode: "slice",
      sliceMode: "transient",
      slicePoints: [0, 0.25, 0.5, 1],
    }));
    const sliceBtns = Array.from(container.querySelectorAll(".sampler-slice-btn"))
      .filter((b) => b.textContent === "Slice 2");
    expect(sliceBtns.length).toBe(1);
    fireEvent.click(sliceBtns[0]);
    await flushRead();
    expect(mockedCall).toHaveBeenCalledWith("sampler.triggerSlice", { trackIndex: 0, slotIndex: 0, sliceIndex: 1 });
  });

  it("renders the No slices hint when slicePoints is empty", async () => {
    const { container } = await renderSampler(mockSamplerState({ mode: "slice" }));
    expect(container.textContent).toContain("No slices yet");
  });
});