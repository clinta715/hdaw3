import { describe, it, expect, beforeEach } from "vitest";
import { useProjectStore } from "../store/projectStore";
import { ProjectSnapshot } from "../rpc/types";

const mockSnapshot: ProjectSnapshot = {
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
      color: 0xff0000,
      volume: 0.8,
      pan: 0,
      muted: false,
      soloed: false,
      armed: false,
      inputMonitor: false,
      height: 80,
      midiChannel: 0,
      clipCount: 2,
    },
    {
      index: 1,
      name: "Drums",
      color: 0x00ff00,
      volume: 1.0,
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
  clips: [
    {
      clipId: 1,
      trackIndex: 0,
      name: "Clip 1",
      sourceFile: "",
      startBeat: 0,
      durationBeats: 4,
      offset: 0,
      gain: 1,
      fadeIn: 0,
      fadeOut: 0,
      looping: false,
      muted: false,
      isMidi: true,
      sourceBpm: 0,
      stretchMode: 0,
      stretchRatio: 1,
      sourceDuration: 0,
      isGhost: false,
      ghostSourceId: 0,
      gainEnvelope: [],
    },
  ],
  scaleRoot: 0,
  scaleMode: 0,
};

describe("projectStore", () => {
  beforeEach(() => {
    useProjectStore.setState({
      snapshot: null,
      notesByClip: new Map(),
      lastSync: 0,
      isDirty: false,
      filePath: null,
    });
  });

  it("starts with null snapshot", () => {
    expect(useProjectStore.getState().snapshot).toBeNull();
    expect(useProjectStore.getState().isDirty).toBe(false);
  });

  it("gets track by index", () => {
    useProjectStore.setState({ snapshot: mockSnapshot });
    const track = useProjectStore.getState().getTrack(0);
    expect(track).toBeDefined();
    expect(track!.name).toBe("Synth");
  });

  it("returns undefined for invalid track index", () => {
    useProjectStore.setState({ snapshot: mockSnapshot });
    expect(useProjectStore.getState().getTrack(99)).toBeUndefined();
  });

  it("gets clip by id", () => {
    useProjectStore.setState({ snapshot: mockSnapshot });
    const clip = useProjectStore.getState().getClip(1);
    expect(clip).toBeDefined();
    expect(clip!.name).toBe("Clip 1");
  });

  it("returns undefined for invalid clip id", () => {
    useProjectStore.setState({ snapshot: mockSnapshot });
    expect(useProjectStore.getState().getClip(999)).toBeUndefined();
  });

  it("sets file path", () => {
    useProjectStore.getState().setFilePath("/path/to/project.hdaw");
    expect(useProjectStore.getState().filePath).toBe("/path/to/project.hdaw");
  });

  it("sets dirty flag", () => {
    useProjectStore.setState({ isDirty: true });
    expect(useProjectStore.getState().isDirty).toBe(true);
  });
});
