import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, cleanup, act } from "@testing-library/react";
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

describe("SamplerEditor", () => {
  beforeEach(() => {
    mockedCall.mockReset();
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
    mockedCall
      .mockResolvedValueOnce([{ fxType: "sampler" }])
      .mockResolvedValueOnce({
        sampleFile: "",
        mode: "classic",
        rootNote: 60,
        transpose: 0,
        mono: false,
        playReverse: false,
        envelope: { attack: 0.005, decay: 0.1, sustain: 0.9, release: 0.1 },
        hasSound: false,
        activeVoices: 0,
      });
    render(<SamplerEditor />);
    await flushRead();
    expect(screen.getByText("Mode")).toBeTruthy();
    expect(screen.getByText("Root")).toBeTruthy();
    expect(screen.getByText("Transpose")).toBeTruthy();
    expect(screen.getByText("Mono")).toBeTruthy();
    expect(screen.getByText("A")).toBeTruthy();
    expect(screen.getByText("D")).toBeTruthy();
    expect(screen.getByText("S")).toBeTruthy();
    expect(screen.getByText("R")).toBeTruthy();
  });
});
