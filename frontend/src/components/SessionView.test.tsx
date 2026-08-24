import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen } from "@testing-library/react";
import SessionView from "./SessionView";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue(null) },
}));

function makeTracks(n: number) {
  return Array.from({ length: n }, (_, i) => ({
    index: i,
    name: `Track ${i + 1}`,
    color: 0x4488cc,
    volume: 1,
    pan: 0,
    muted: false,
    soloed: false,
    armed: false,
    inputMonitor: false,
    height: 80,
    midiChannel: 0,
    clipCount: 0,
    trackType: 0,
    effectiveMuted: false,
    effectiveSoloed: false,
  }));
}

describe("SessionView", () => {
  beforeEach(() => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
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
        tracks: makeTracks(3),
        clips: [],
        scaleRoot: 0,
        scaleMode: 0,
        launchedScene: -1,
        sceneCount: 8,
      },
    } as any);
    useUiStore.setState({ viewMode: "session" });
  });

  it("renders scene buttons with default names when no clips", () => {
    render(<SessionView />);
    expect(screen.getByRole("button", { name: "Scene 1" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Scene 8" })).toBeTruthy();
  });

  it("renders clip slot grid", () => {
    render(<SessionView />);
    const slots = document.querySelectorAll(".sv-slot");
    expect(slots.length).toBe(24); // 3 tracks x 8 scenes
  });

  it("shows clip name as scene label from first track", () => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: {
          bpm: 120, isPlaying: false, isLooping: false, isRecording: false,
          loopStart: 0, loopEnd: 8, currentTimeSeconds: 0, sampleRate: 44100,
        },
        tracks: makeTracks(3),
        clips: [
          { clipId: 1, trackIndex: 0, sceneIndex: 0, name: "Lead Phrase 1", sourceFile: "", startBeat: 0, durationBeats: 4, gain: 1, fade_in: 0, fade_out: 0, looping: false, muted: false, isMidi: true, ghost: false, takes: [] },
          { clipId: 2, trackIndex: 0, sceneIndex: 1, name: "Bass Groove", sourceFile: "", startBeat: 0, durationBeats: 4, gain: 1, fade_in: 0, fade_out: 0, looping: false, muted: false, isMidi: true, ghost: false, takes: [] },
        ],
        scaleRoot: 0,
        scaleMode: 0,
        launchedScene: -1,
        sceneCount: 8,
      },
    } as any);
    render(<SessionView />);
    expect(screen.getByRole("button", { name: "Lead Phrase 1" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Bass Groove" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Scene 3" })).toBeTruthy();
  });
});
