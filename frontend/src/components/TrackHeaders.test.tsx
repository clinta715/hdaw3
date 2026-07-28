import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import TrackHeaders from "./TrackHeaders";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";
import { useUiStore } from "../store/uiStore";
import type { TrackSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkTrack(index: number, over: Partial<TrackSnapshot> = {}): TrackSnapshot {
  return {
    index, name: `Track ${index}`, color: 0x5b9bd5, volume: 1, pan: 0,
    muted: false, soloed: false, armed: false, inputMonitor: false,
    height: 56, midiChannel: 0, clipCount: 0, trackType: 0,
    effectiveMuted: false, effectiveSoloed: false, ...over,
  };
}

function setTracks(tracks: TrackSnapshot[]) {
  useProjectStore.setState({
    snapshot: {
      name: "T",
      transport: {
        bpm: 120, isPlaying: false, isLooping: false, isRecording: false,
        loopStart: 0, loopEnd: 8, currentTimeSeconds: 0, sampleRate: 44100,
      },
      tracks, clips: [], scaleRoot: 0, scaleMode: 0,
    },
  });
}

describe("TrackHeaders", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
    useMeterStore.setState({ master: { l: 0, r: 0 }, tracks: [] });
    useUiStore.setState({ selectedClipIds: new Set(), selectedTrackIndex: null });
  });

  afterEach(() => cleanup());

  it("renders a row per track", () => {
    setTracks([mkTrack(0), mkTrack(1)]);
    render(<TrackHeaders />);
    expect(screen.getByText("Track 0")).toBeInTheDocument();
    expect(screen.getByText("Track 1")).toBeInTheDocument();
  });

  it("sizes each row from the track's persisted height", () => {
    setTracks([mkTrack(0, { height: 90 })]);
    const { container } = render(<TrackHeaders />);
    const row = container.querySelector(".th-row") as HTMLElement;
    expect(row.style.height).toBe("90px");
  });

  it("shows an empty state whose Add Track action offers a type choice", () => {
    setTracks([]);
    render(<TrackHeaders />);
    expect(screen.getByText("No tracks loaded")).toBeInTheDocument();
    fireEvent.click(screen.getByText("+ Add Track"));
    fireEvent.click(screen.getByText("Audio Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.addTrack", { trackType: 0 });
  });

  it("right-clicking a row opens a menu with Add/Duplicate/Delete Track", () => {
    setTracks([mkTrack(0), mkTrack(1)]);
    const { container } = render(<TrackHeaders />);
    fireEvent.contextMenu(container.querySelectorAll(".th-row")[1]);
    expect(screen.getByText("Add Audio Track")).toBeInTheDocument();
    expect(screen.getByText("Add MIDI Track")).toBeInTheDocument();
    expect(screen.getByText("Duplicate Track")).toBeInTheDocument();
    expect(screen.getByText("Delete Track")).toBeInTheDocument();
  });

  it("Delete Track from the header menu removes that track", () => {
    setTracks([mkTrack(0), mkTrack(1)]);
    const { container } = render(<TrackHeaders />);
    fireEvent.contextMenu(container.querySelectorAll(".th-row")[1]);
    fireEvent.mouseDown(screen.getByText("Delete Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.removeTrack", { trackIndex: 1 });
  });

  it("Duplicate Track from the header menu duplicates that track", () => {
    setTracks([mkTrack(0), mkTrack(1)]);
    const { container } = render(<TrackHeaders />);
    fireEvent.contextMenu(container.querySelectorAll(".th-row")[0]);
    fireEvent.mouseDown(screen.getByText("Duplicate Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.duplicateTrack", { trackIndex: 0 });
  });

  it("Set Type: MIDI from the header menu retypes the track", () => {
    setTracks([mkTrack(0, { trackType: 0 })]);
    const { container } = render(<TrackHeaders />);
    fireEvent.contextMenu(container.querySelector(".th-row") as HTMLElement);
    fireEvent.mouseDown(screen.getByText("Set Type: MIDI"));
    expect(mockedCall).toHaveBeenCalledWith("project.setTrackType", { trackIndex: 0, trackType: 1 });
  });
});
