import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import ExportDialog from "./ExportDialog";
import { rpc } from "../rpc";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useMarkerStore } from "../store/markerStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn(), onNotification: vi.fn(() => vi.fn()) },
}));

vi.mock("../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function getRowControl(labelText: string): HTMLInputElement | HTMLSelectElement {
  const label = screen.getByText(labelText);
  const row = label.closest(".ed-row")!;
  return row.querySelector("input, select") as HTMLInputElement | HTMLSelectElement;
}

function seedStores(overrides?: {
  transport?: Partial<ReturnType<typeof useTransportStore.getState>["transport"]>;
  snapshot?: Partial<NonNullable<ReturnType<typeof useProjectStore.getState>["snapshot"]>>;
  selectedClipIds?: Set<string>;
  markers?: Array<{ index: number; name: string; time: number; color: number }>;
}) {
  useTransportStore.setState({
    transport: {
      bpm: 120,
      isPlaying: false,
      isLooping: false,
      loopStart: 0,
      loopEnd: 4,
      isRecording: false,
      currentTimeSeconds: 0,
      sampleRate: 44100,
      timeSigNumerator: 4,
      timeSigDenominator: 4,
      ...overrides?.transport,
    },
  });
  useProjectStore.setState({
    snapshot: {
      name: "Test",
      transport: {
        bpm: 120,
        isPlaying: false,
        isLooping: false,
        isRecording: false,
        loopStart: 0,
        loopEnd: 0,
        currentTimeSeconds: 0,
        sampleRate: 44100,
        timeSigNumerator: 4,
        timeSigDenominator: 4,
      },
      tracks: [],
      clips: [],
      masterGain: 1,
      scaleRoot: 0,
      scaleMode: 0,
      ...overrides?.snapshot,
    } as any,
  });
  useUiStore.setState({ selectedClipIds: overrides?.selectedClipIds ?? new Set() });
  useMarkerStore.setState({ markers: overrides?.markers ?? [] });
}

describe("ExportDialog", () => {
  beforeEach(() => {
    window.localStorage.clear();
    mockedCall.mockReset();
    seedStores();
  });

  afterEach(() => {
    cleanup();
  });

  it("renders 'Export Audio' title", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    expect(screen.getByText("Export Audio")).toBeInTheDocument();
  });

  it("renders format select with WAV, AIFF, FLAC options", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    expect(screen.getByRole("option", { name: "WAV" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "AIFF" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "FLAC" })).toBeInTheDocument();
  });

  it("renders bit depth select with 16, 24, 32 options", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    expect(screen.getByRole("option", { name: "16-bit" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "24-bit" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "32-bit float" })).toBeInTheDocument();
  });

  it("renders sample rate select with 44100, 48000, 96000", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    expect(screen.getByRole("option", { name: "44100 Hz" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "48000 Hz" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "96000 Hz" })).toBeInTheDocument();
  });

  it("renders range select with Full, Loop, Selection, Markers", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    expect(screen.getByRole("option", { name: "Full Project" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "Loop Region" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "Selection" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "Between Markers" })).toBeInTheDocument();
  });

  it("default output path is 'export.wav'", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    const input = getRowControl("Output") as HTMLInputElement;
    expect(input.value).toBe("export.wav");
  });

  it("changing format updates the output filename extension", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    const formatSelect = getRowControl("Format") as HTMLSelectElement;
    const outputInput = getRowControl("Output") as HTMLInputElement;

    fireEvent.change(formatSelect, { target: { value: "aiff" } });
    expect(outputInput.value).toBe("export.aiff");

    fireEvent.change(formatSelect, { target: { value: "flac" } });
    expect(outputInput.value).toBe("export.flac");

    fireEvent.change(formatSelect, { target: { value: "wav" } });
    expect(outputInput.value).toBe("export.wav");
  });

  it("export button is disabled when output path is empty", () => {
    render(<ExportDialog onClose={vi.fn()} />);
    const input = getRowControl("Output") as HTMLInputElement;
    const exportBtn = screen.getByRole("button", { name: "Export" });

    expect(exportBtn).not.toBeDisabled();

    fireEvent.change(input, { target: { value: "" } });
    expect(exportBtn).toBeDisabled();
  });

  it("clicking Export calls export.audio with correct params", async () => {
    mockedCall.mockResolvedValue({});
    render(<ExportDialog onClose={vi.fn()} />);
    const exportBtn = screen.getByRole("button", { name: "Export" });

    fireEvent.click(exportBtn);

    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("export.audio", {
        outputPath: "export.wav",
        format: "wav",
        bitDepth: 24,
        sampleRate: 48000,
      });
    });
  });

  it("clicking Close calls onClose", () => {
    const onClose = vi.fn();
    render(<ExportDialog onClose={onClose} />);
    const closeBtn = screen.getByRole("button", { name: "Close" });

    fireEvent.click(closeBtn);
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it("export preferences persist to localStorage", async () => {
    render(<ExportDialog onClose={vi.fn()} />);

    fireEvent.change(getRowControl("Format"), { target: { value: "flac" } });
    fireEvent.change(getRowControl("Bit Depth"), { target: { value: "16" } });
    fireEvent.change(getRowControl("Sample Rate"), { target: { value: "96000" } });

    await waitFor(() => {
      const raw = window.localStorage.getItem("hdaw.exportPrefs");
      expect(raw).toBeTruthy();
      const prefs = JSON.parse(raw!);
      expect(prefs.format).toBe("flac");
      expect(prefs.bitDepth).toBe(16);
      expect(prefs.sampleRate).toBe(96000);
    });
  });

  it("export preferences restore from localStorage on mount", () => {
    window.localStorage.setItem(
      "hdaw.exportPrefs",
      JSON.stringify({ format: "aiff", bitDepth: 32, sampleRate: 96000, range: "loop" })
    );

    render(<ExportDialog onClose={vi.fn()} />);

    expect((getRowControl("Format") as HTMLSelectElement).value).toBe("aiff");
    expect((getRowControl("Bit Depth") as HTMLSelectElement).value).toBe("32");
    expect((getRowControl("Sample Rate") as HTMLSelectElement).value).toBe("96000");
    expect((getRowControl("Range") as HTMLSelectElement).value).toBe("loop");
  });

  it("renders without crashing", () => {
    const { unmount } = render(<ExportDialog onClose={vi.fn()} />);
    unmount();
  });
});
