import { describe, it, expect, beforeEach } from "vitest";
import { useProjectStore } from "../store/projectStore";
import { ProjectSnapshot } from "../rpc/types";
import type { TreeDelta, ClipSnapshot, TrackSnapshot } from "../rpc/types";

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

const mkClip = (clipId: number, trackIndex: number, startBeat: number): ClipSnapshot => ({
  clipId, trackIndex, name: `Clip ${clipId}`, sourceFile: "", startBeat,
  durationBeats: 4, offset: 0, gain: 1, fadeIn: 0, fadeOut: 0, looping: false,
  muted: false, isMidi: true, sourceBpm: 0, stretchMode: 0, stretchRatio: 1,
  sourceDuration: 0, isGhost: false, ghostSourceId: -1, gainEnvelope: [],
});

describe("applyDelta", () => {
  beforeEach(() => {
    useProjectStore.setState({ snapshot: structuredClone(mockSnapshot), lastSync: 0 });
  });

  it("upserts a new clip", () => {
    const before = useProjectStore.getState().snapshot!.clips.length;
    const delta: TreeDelta = { fullSync: false, clipsUpserted: [mkClip(999, 0, 16)] };
    useProjectStore.getState().applyDelta(delta);
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.length).toBe(before + 1);
    expect(clips.find((c) => c.clipId === 999)?.startBeat).toBe(16);
  });

  it("replaces an existing clip in place", () => {
    const existing = useProjectStore.getState().snapshot!.clips[0];
    const updated = { ...existing, startBeat: 123 };
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [updated] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.filter((c) => c.clipId === existing.clipId).length).toBe(1);
    expect(clips.find((c) => c.clipId === existing.clipId)?.startBeat).toBe(123);
  });

  it("removes clips by id", () => {
    const target = useProjectStore.getState().snapshot!.clips[0];
    const before = useProjectStore.getState().snapshot!.clips.length;
    useProjectStore.getState().applyDelta({ fullSync: false, clipsRemoved: [target.clipId] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.length).toBe(before - 1);
    expect(clips.find((c) => c.clipId === target.clipId)).toBeUndefined();
  });

  it("upserts a track and keeps tracks sorted by index", () => {
    const t: TrackSnapshot = { ...useProjectStore.getState().snapshot!.tracks[0], volume: 0.25 };
    useProjectStore.getState().applyDelta({ fullSync: false, tracksUpserted: [t] });
    const tracks = useProjectStore.getState().snapshot!.tracks;
    expect(tracks.find((x) => x.index === 0)?.volume).toBe(0.25);
    for (let i = 1; i < tracks.length; i++) expect(tracks[i].index).toBeGreaterThan(tracks[i - 1].index);
  });

  it("keeps object references for unchanged clips", () => {
    const clipsBefore = useProjectStore.getState().snapshot!.clips;
    const unchanged = clipsBefore[clipsBefore.length - 1];
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [mkClip(999, 0, 0)] });
    const clipsAfter = useProjectStore.getState().snapshot!.clips;
    expect(clipsAfter.find((c) => c.clipId === unchanged.clipId)).toBe(unchanged); // same reference
  });

  it("is a no-op when there is no snapshot yet", () => {
    useProjectStore.setState({ snapshot: null });
    expect(() => useProjectStore.getState().applyDelta({ fullSync: false, clipsRemoved: [1] })).not.toThrow();
    expect(useProjectStore.getState().snapshot).toBeNull();
  });
});

describe("pending placeholders", () => {
  beforeEach(() => {
    useProjectStore.setState({
      snapshot: structuredClone(mockSnapshot),
      lastSync: 0,
      pendingTempIds: new Set(),
      pendingResolution: new Map(),
    });
  });

  it("addPendingClip appends the placeholder and records its temp id", () => {
    const before = useProjectStore.getState().snapshot!.clips.length;
    useProjectStore.getState().addPendingClip(mkClip(-1, 0, 8));
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.length).toBe(before + 1);
    expect(clips.find((c) => c.clipId === -1)).toBeDefined();
    expect(useProjectStore.getState().pendingTempIds.has(-1)).toBe(true);
  });

  it("removePending removes the placeholder and prunes state", () => {
    useProjectStore.getState().addPendingClip(mkClip(-1, 0, 8));
    useProjectStore.getState().removePending(-1);
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.find((c) => c.clipId === -1)).toBeUndefined();
    expect(useProjectStore.getState().pendingTempIds.has(-1)).toBe(false);
    expect(useProjectStore.getState().pendingResolution.has(-1)).toBe(false);
  });

  it("applyDelta swaps a resolved placeholder for the real clip", () => {
    useProjectStore.getState().addPendingClip(mkClip(-1, 0, 8));
    useProjectStore.getState().resolvePending(-1, 500);
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [mkClip(500, 0, 8)] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.find((c) => c.clipId === 500)).toBeDefined();
    expect(clips.find((c) => c.clipId === -1)).toBeUndefined();
    expect(useProjectStore.getState().pendingTempIds.has(-1)).toBe(false);
    expect(useProjectStore.getState().pendingResolution.has(-1)).toBe(false);
  });

  it("applyDelta leaves unrelated placeholders pending", () => {
    useProjectStore.getState().addPendingClip(mkClip(-1, 0, 8));
    useProjectStore.getState().addPendingClip(mkClip(-2, 0, 12));
    useProjectStore.getState().resolvePending(-1, 500);
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [mkClip(500, 0, 8)] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.find((c) => c.clipId === -1)).toBeUndefined();
    expect(clips.find((c) => c.clipId === -2)).toBeDefined();
    expect(useProjectStore.getState().pendingTempIds.has(-2)).toBe(true);
  });

  it("applyDelta is a reference-stable no-op for clips when pendingResolution is empty", () => {
    const clipsBefore = useProjectStore.getState().snapshot!.clips;
    useProjectStore.getState().applyDelta({ fullSync: false });
    const clipsAfter = useProjectStore.getState().snapshot!.clips;
    expect(clipsAfter).toBe(clipsBefore);
  });

  it("addPendingClip is a no-op without a snapshot", () => {
    useProjectStore.setState({ snapshot: null });
    expect(() => useProjectStore.getState().addPendingClip(mkClip(-1, 0, 0))).not.toThrow();
    expect(useProjectStore.getState().snapshot).toBeNull();
  });
});
