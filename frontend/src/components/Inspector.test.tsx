import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import Inspector from "./Inspector";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";
import type { TrackSnapshot, ClipSnapshot, ProjectSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkTrack(over: Partial<TrackSnapshot> = {}): TrackSnapshot {
  return {
    index: 1, name: "Synth", color: 0x5b9bd5, volume: 0.8, pan: 0,
    muted: false, soloed: false, armed: false, inputMonitor: false,
    height: 80, midiChannel: 0, clipCount: 3, trackType: 0,
    effectiveMuted: false, effectiveSoloed: false, ...over,
  };
}

function mkClip(over: Partial<ClipSnapshot> = {}): ClipSnapshot {
  return {
    clipId: 100, trackIndex: 1, name: "Kick", sourceFile: "samples/kick.wav",
    startBeat: 1, durationBeats: 4, offset: 0, gain: 1, fadeIn: 0, fadeOut: 0,
    looping: false, muted: false, isMidi: false, sourceBpm: 120,
    stretchMode: 0, stretchRatio: 1, sourceDuration: 4, isGhost: false,
    ghostSourceId: -1, gainEnvelope: [], ...over,
  };
}

function mkSnapshot(tracks: TrackSnapshot[], clips: ClipSnapshot[]): ProjectSnapshot {
  return {
    name: "test",
    transport: { bpm: 120, isPlaying: false, isLooping: false, isRecording: false, loopStart: 0, loopEnd: 0, currentTimeSeconds: 0, sampleRate: 44100 },
    tracks, clips,
    scaleRoot: 0, scaleMode: 0,
  };
}

describe("Inspector", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
  });

  afterEach(() => cleanup());

  it("renders empty message when no selection", () => {
    useUiStore.setState({ selectedTrackIndex: null, selectedClipIds: new Set() });
    useProjectStore.setState({ snapshot: mkSnapshot([], []) });
    render(<Inspector />);
    expect(screen.getByText("Select a track or clip to inspect.")).toBeInTheDocument();
  });

  it("renders TrackInspector when a track is selected", () => {
    useUiStore.setState({ selectedTrackIndex: 0, selectedClipIds: new Set() });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack({ index: 0 })], []) });
    render(<Inspector />);
    expect(screen.getByText("Track 0")).toBeInTheDocument();
    expect(screen.getByDisplayValue("Synth")).toBeInTheDocument();
  });

  it("renders ClipInspector when exactly one clip is selected", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], [mkClip()]) });
    render(<Inspector />);
    expect(screen.getByText("Clip 100")).toBeInTheDocument();
    expect(screen.getByText("Kick")).toBeInTheDocument();
  });

  it("falls back to TrackInspector when multiple clips and a track are selected", () => {
    const tracks = [mkTrack({ index: 0 }), mkTrack({ index: 1 })];
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100, 101]) });
    useProjectStore.setState({ snapshot: mkSnapshot(tracks, [mkClip(), mkClip({ clipId: 101 })]) });
    render(<Inspector />);
    expect(screen.getByText("Track 1")).toBeInTheDocument();
  });

  it("renders empty message when clip is selected but not in snapshot", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([999]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], []) });
    render(<Inspector />);
    expect(screen.getByText("Select a track or clip to inspect.")).toBeInTheDocument();
  });

  it("renders empty message when track index is out of bounds", () => {
    useUiStore.setState({ selectedTrackIndex: 99, selectedClipIds: new Set() });
    useProjectStore.setState({ snapshot: mkSnapshot([], []) });
    render(<Inspector />);
    expect(screen.getByText("Select a track or clip to inspect.")).toBeInTheDocument();
  });

  it("clicking mute toggle calls setTrackMuted", () => {
    useUiStore.setState({ selectedTrackIndex: 0, selectedClipIds: new Set() });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack({ index: 0, muted: false })], []) });
    render(<Inspector />);
    const muteBtn = screen.getByText("M");
    fireEvent.click(muteBtn);
    expect(mockedCall).toHaveBeenCalledWith("project.setTrackMuted", { trackIndex: 0, muted: true });
  });

  it("clicking clip mute toggle calls setClipMuted", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], [mkClip({ muted: false })]) });
    render(<Inspector />);
    const muteBtn = screen.getByText("Muted");
    fireEvent.click(muteBtn);
    expect(mockedCall).toHaveBeenCalledWith("project.setClipMuted", { clipId: 100, muted: true });
  });

  it("renders track type dropdown", () => {
    useUiStore.setState({ selectedTrackIndex: 0, selectedClipIds: new Set() });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack({ index: 0 })], []) });
    render(<Inspector />);
    const select = screen.getByRole("combobox") as HTMLSelectElement;
    expect(select.value).toBe("0");
  });

  it("renders stretching mode dropdown for clip", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], [mkClip({ stretchMode: 1 })]) });
    render(<Inspector />);
    const select = screen.getByRole("combobox") as HTMLSelectElement;
    expect(select.value).toBe("1");
  });

  it("renders gain envelope readout", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], [mkClip({ gainEnvelope: [{ time: 0, gain: 1 }, { time: 2, gain: 0.5 }] })]) });
    render(<Inspector />);
    expect(screen.getByText("2 points")).toBeInTheDocument();
  });

  it("renders gain envelope none readout", () => {
    useUiStore.setState({ selectedTrackIndex: 1, selectedClipIds: new Set([100]) });
    useProjectStore.setState({ snapshot: mkSnapshot([mkTrack()], [mkClip({ gainEnvelope: [] })]) });
    render(<Inspector />);
    expect(screen.getAllByText("none").length).toBeGreaterThan(0);
  });
});