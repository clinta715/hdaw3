import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup, fireEvent } from "@testing-library/react";
import AudioClipEditor from "./AudioClipEditor";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useUiStore } from "../store/uiStore";

global.ResizeObserver = class {
  observe() {}
  unobserve() {}
  disconnect() {}
};

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue(null) },
}));

vi.mock("./WaveformCanvas", () => ({
  WaveformCanvas: ({ clip, onError }: { clip?: { sourceFile?: string }; onError?: (v: boolean) => void }) => {
    if (onError) onError(!clip?.sourceFile);
    return <div data-testid="waveform-canvas" />;
  },
}));

vi.mock("../store/notifyStore", () => ({
  useNotifyStore: { getState: vi.fn(() => ({ push: vi.fn() })) },
  reportRpcError: vi.fn(),
}));

const mkClip = (overrides: Record<string, unknown> = {}) => ({
  clipId: 1,
  trackIndex: 0,
  name: "Test Audio Clip",
  sourceFile: "/samples/test.wav",
  startBeat: 0,
  durationBeats: 8,
  offset: 0,
  gain: 1,
  fadeIn: 0,
  fadeOut: 0,
  looping: false,
  muted: false,
  isMidi: false,
  sourceBpm: 120,
  stretchMode: 0,
  stretchRatio: 1,
  sourceDuration: 4,
  isGhost: false,
  ghostSourceId: 0,
  gainEnvelope: [],
  activeTake: 0,
  takeCount: 0,
  takes: [],
  ...overrides,
});

const mkTrack = (overrides: Record<string, unknown> = {}) => ({
  index: 0,
  name: "Track 1",
  color: 0xff0000,
  volume: 1,
  pan: 0,
  muted: false,
  soloed: false,
  armed: false,
  inputMonitor: false,
  height: 80,
  midiChannel: 0,
  clipCount: 1,
  trackType: 0,
  effectiveMuted: false,
  effectiveSoloed: false,
  ...overrides,
});

describe("AudioClipEditor", () => {
  beforeEach(() => {
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack()],
        clips: [mkClip()],
        masterGain: 1,
      },
    });
    useTransportStore.setState({
      transport: {
        bpm: 120,
        isPlaying: false,
        isLooping: false,
        isRecording: false,
        loopStart: 0,
        loopEnd: 8,
        currentTimeSeconds: 0,
        sampleRate: 44100,
      },
    });
    useUiStore.setState({ selectedClipIds: new Set([1]) });
  });

  afterEach(() => cleanup());

  it("renders without crashing", () => {
    const { container } = render(<AudioClipEditor />);
    expect(container.querySelector(".audio-clip-editor")).toBeInTheDocument();
  });

  it("displays the clip name", () => {
    const { getByText } = render(<AudioClipEditor />);
    expect(getByText("Test Audio Clip")).toBeInTheDocument();
  });

  it("shows gain slider (input[type='range'])", () => {
    const { container } = render(<AudioClipEditor />);
    const gainInput = container.querySelector('input[type="range"]');
    expect(gainInput).toBeInTheDocument();
  });

  it("gain slider range is set correctly", () => {
    const { container } = render(<AudioClipEditor />);
    const gainInput = container.querySelector('input[type="range"]') as HTMLInputElement;
    expect(gainInput.min).toBe("0");
    expect(gainInput.max).toBe("2");
    expect(gainInput.step).toBe("0.01");
  });

  it("changing gain and mouseUp fires project.setClipGain", async () => {
    const { rpc } = await import("../rpc");
    const { container } = render(<AudioClipEditor />);
    const gainInput = container.querySelector('input[type="range"]') as HTMLInputElement;
    fireEvent.change(gainInput, { target: { value: "1.5" } });
    fireEvent.mouseUp(gainInput);
    expect(rpc.call).toHaveBeenCalledWith("project.setClipGain", {
      clipId: 1,
      gain: 1.5,
    });
  });

  it("gainToDb pure function works (indirectly via rendered dB display)", () => {
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack()],
        clips: [mkClip({ gain: 0.5 })],
        masterGain: 1,
      },
    });
    const { getByText } = render(<AudioClipEditor />);
    expect(getByText("-6.0 dB")).toBeInTheDocument();
  });

  it("dbToGain pure function works (indirectly via rendered dB display)", () => {
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack()],
        clips: [mkClip({ gain: 0 })],
        masterGain: 1,
      },
    });
    const { getByText } = render(<AudioClipEditor />);
    expect(getByText("-inf dB")).toBeInTheDocument();
  });

  it("shows file-missing banner when clip source missing", () => {
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack()],
        clips: [mkClip({ sourceFile: "" })],
        masterGain: 1,
      },
    });
    const { getByText } = render(<AudioClipEditor />);
    expect(getByText("Source file not found")).toBeInTheDocument();
  });

  it("shows stretch mode controls (select elements)", () => {
    const { container } = render(<AudioClipEditor />);
    const gearBtn = container.querySelector('.ace-zoom-btn[title="Show advanced controls"]') as HTMLButtonElement;
    fireEvent.click(gearBtn);
    const selects = container.querySelectorAll("select");
    expect(selects.length).toBeGreaterThanOrEqual(1);
  });

  it("renders without crashing with no selected clips", () => {
    useUiStore.setState({ selectedClipIds: new Set() });
    const { container } = render(<AudioClipEditor />);
    expect(container.querySelector(".audio-clip-editor")).toBeInTheDocument();
  });

  it("renders without crashing with multiple clips selected", () => {
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack()],
        clips: [
          mkClip({ clipId: 1, name: "Clip A" }),
          mkClip({ clipId: 2, name: "Clip B" }),
        ],
        masterGain: 1,
      },
    });
    useUiStore.setState({ selectedClipIds: new Set([1, 2]) });
    const { container } = render(<AudioClipEditor />);
    expect(container.querySelector(".audio-clip-editor")).toBeInTheDocument();
  });

  it("loop toggle button exists", () => {
    const { container } = render(<AudioClipEditor />);
    const gearBtn = container.querySelector('.ace-zoom-btn[title="Show advanced controls"]') as HTMLButtonElement;
    fireEvent.click(gearBtn);
    const loopInput = container.querySelector('input[type="checkbox"]');
    expect(loopInput).toBeInTheDocument();
  });
});
