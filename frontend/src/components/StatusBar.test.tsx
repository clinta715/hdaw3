import { describe, it, expect, beforeEach } from "vitest";
import { render, screen, act } from "@testing-library/react";
import StatusBar from "./StatusBar";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";

describe("StatusBar", () => {
  beforeEach(() => {
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
    useProjectStore.setState({
      snapshot: {
        name: "Test Project",
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
        tracks: [
          {
            index: 0,
            name: "Synth",
            color: 0,
            volume: 0.8,
            pan: 0,
            muted: false,
            soloed: false,
            armed: false,
            inputMonitor: false,
            height: 80,
            midiChannel: 0,
            clipCount: 0,
          },
        ],
        clips: [],
        scaleRoot: 0,
        scaleMode: 0,
      },
      notesByClip: new Map(),
      lastSync: 0,
      isDirty: false,
      filePath: null,
    });
    useUiStore.setState({
      selectedClipIds: new Set(),
      lastSelectedClipId: null,
      selectedTrackIndex: null,
      clipClipboard: [],
      activeBottomTab: "mixer",
      snapEnabled: true,
      snapDivision: 1,
      showPhraseGenerator: false,
    });
  });

  it("renders BPM", () => {
    render(<StatusBar />);
    expect(screen.getByText(/120.0/)).toBeInTheDocument();
  });

  it("renders sample rate", () => {
    render(<StatusBar />);
    expect(screen.getByText(/44100 Hz/)).toBeInTheDocument();
  });

  it("renders selected count", () => {
    render(<StatusBar />);
    expect(screen.getByText(/0 selected/)).toBeInTheDocument();
  });

  it("shows track name when track is selected", () => {
    useUiStore.setState({ selectedTrackIndex: 0 });
    render(<StatusBar />);
    expect(screen.getByText(/Track: Synth/)).toBeInTheDocument();
  });

  it("shows recording indicator when recording", () => {
    useTransportStore.getState().update({
      bpm: 120,
      isPlaying: true,
      isLooping: false,
      isRecording: true,
      loopStart: 0,
      loopEnd: 8,
      currentTimeSeconds: 0,
      sampleRate: 44100,
    });
    render(<StatusBar />);
    expect(screen.getByText(/REC/)).toBeInTheDocument();
  });

  it("updates when selection changes", () => {
    const { rerender } = render(<StatusBar />);
    expect(screen.getByText(/0 selected/)).toBeInTheDocument();

    act(() => {
      useUiStore.setState({ selectedClipIds: new Set([1, 2, 3]) });
    });
    rerender(<StatusBar />);
    expect(screen.getByText(/3 selected/)).toBeInTheDocument();
  });
});
