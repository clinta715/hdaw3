import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useTimelineDrag } from "./useTimelineDrag";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import type { ClipSnapshot } from "../rpc/types";

const mockRpc = {
  call: vi.fn(),
};

const makeEngagementRef = () => ({ current: "none" as "none" | "clip" | "rubber" });

const makeClip = (clipId: number, trackIndex: number, startBeat: number): ClipSnapshot => ({
  clipId,
  trackIndex,
  name: `Clip ${clipId}`,
  sourceFile: "",
  startBeat,
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
});

const makeTracksRef = () => {
  const ref = { current: document.createElement("div") };
  ref.current.getBoundingClientRect = () => ({
    left: 0, top: 0, width: 1000, height: 56, right: 1000, bottom: 56, x: 0, y: 0,
    toJSON: () => {},
  });
  ref.current.scrollLeft = 0;
  return ref;
};

const withRpc = (impl: (method: string) => unknown) =>
  (mockRpc.call as ReturnType<typeof vi.fn>).mockImplementation(
    (method: string) => Promise.resolve(impl(method))
  );

describe("useTimelineDrag", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: {
          bpm: 120, isPlaying: false, isLooping: false, isRecording: false,
          loopStart: 0, loopEnd: 8, currentTimeSeconds: 0, sampleRate: 44100,
        },
        tracks: [
          {
            index: 0, name: "Track 1", color: 0, volume: 1, pan: 0,
            muted: false, soloed: false, armed: false, inputMonitor: false,
            height: 56, midiChannel: 0, clipCount: 1,
          },
        ],
        clips: [makeClip(1, 0, 0)],
        scaleRoot: 0,
        scaleMode: 0,
      },
      notesByClip: new Map(),
      lastSync: 0,
      isDirty: false,
      filePath: null,
    });
    useUiStore.setState({
      selectedClipIds: new Set([1]),
      lastSelectedClipId: 1,
      selectedTrackIndex: 0,
      clipClipboard: [],
      activeBottomTab: "mixer",
      snapEnabled: true,
      snapDivision: 1,
      showPhraseGenerator: false,
    });
  });

  describe("normal drag — batch moveClips", () => {
    it("optimistic update + single moveClips RPC", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      mockRpc.call.mockResolvedValue(null);

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28 } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 200, clientY: 28 }));
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      // Optimistic update happens synchronously.
      const clipsAfterMouseUp = useProjectStore.getState().snapshot?.clips;
      expect(clipsAfterMouseUp?.[0].startBeat).toBeGreaterThan(0);

      // Single batch RPC.
      await vi.waitFor(() => {
        expect(mockRpc.call).toHaveBeenCalledWith("project.moveClips",
          expect.objectContaining({ clipIds: [1], newStarts: [expect.any(Number)], newTrackIndices: [0] })
        );
      });
      // No begin/end transaction — moveClips wraps its own.
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.beginTransaction", expect.anything());
    });
  });

  describe("ctrl-drag (duplicate) — batch duplicateClips", () => {
    it("does NOT duplicate on mousemove; duplicates only on mouseup", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.duplicateClips" ? [2] : null));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, ctrlKey: true } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 200, clientY: 28 }));
      });

      expect(mockRpc.call).not.toHaveBeenCalledWith("project.duplicateClips", expect.anything());

      act(() => {
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(mockRpc.call).toHaveBeenCalledWith("project.duplicateClips", expect.objectContaining({ clipIds: [1] }));
      });
    });

    it("swaps selection to the new duplicate id after ctrl-drag", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.duplicateClips" ? [2] : {}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, ctrlKey: true } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 200, clientY: 28 }));
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(useUiStore.getState().selectedClipIds.has(2)).toBe(true);
      });
      expect(useUiStore.getState().selectedClipIds.has(1)).toBe(false);
    });

    it("places the duplicate at the drop target in a single duplicateClips call", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.duplicateClips" ? [2] : {}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, ctrlKey: true } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 250, clientY: 28 }));
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(mockRpc.call).toHaveBeenCalledWith(
          "project.duplicateClips",
          expect.objectContaining({
            clipIds: [1],
            newStarts: [expect.any(Number)],
            newTrackIndices: [0],
          })
        );
      });
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.duplicateClipTo", expect.anything());
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.beginTransaction", expect.anything());
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.endTransaction", expect.anything());
    });

    it("duplicateClips wraps the batch in one call (no separate begin/end transaction)", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.duplicateClips" ? [2] : {}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, ctrlKey: true } as any,
          1, 0, 0
        );
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 200, clientY: 28 }));
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(mockRpc.call).toHaveBeenCalledWith("project.duplicateClips", expect.anything());
      });
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.beginTransaction", expect.anything());
      expect(mockRpc.call).not.toHaveBeenCalledWith("project.endTransaction", expect.anything());
    });
  });

  describe("alt-drag (paint repeat) — atomic paintClips RPC", () => {
    it("does NOT call paintClips during the drag, only on mouseup", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.paintClips" ? [2, 3] : {}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, altKey: true } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 500, clientY: 28 }));
      });

      expect(mockRpc.call).not.toHaveBeenCalledWith("project.paintClips", expect.anything());

      act(() => {
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(mockRpc.call).toHaveBeenCalledWith(
          "project.paintClips",
          expect.objectContaining({ sourceClipIds: [1], targetTrackIndex: 0 })
        );
      });
    });

    it("swaps selection to the painted clip ids", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc((m) => (m === "project.paintClips" ? [2, 3] : {}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, altKey: true } as any,
          1, 0, 0
        );
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 500, clientY: 28 }));
        window.dispatchEvent(new MouseEvent("mouseup"));
      });

      await vi.waitFor(() => {
        expect(useUiStore.getState().selectedClipIds.has(2)).toBe(true);
        expect(useUiStore.getState().selectedClipIds.has(3)).toBe(true);
      });
    });

    it("reports the intended count via paintCount before commit", async () => {
      const clips = [makeClip(1, 0, 0)];
      const tracksRef = makeTracksRef();
      withRpc(() => ({}));

      const { result } = renderHook(() =>
        useTimelineDrag({ clips, pps: 100, TRACK_HEIGHT: 56, tracksRef, trackCount: 1, rpc: mockRpc as any, engagementRef: makeEngagementRef() })
      );

      act(() => {
        result.current.handleClipMouseDown(
          { preventDefault: () => {}, currentTarget: document.createElement("div"), clientX: 50, clientY: 28, altKey: true } as any,
          1, 0, 0
        );
      });
      act(() => {
        window.dispatchEvent(new MouseEvent("mousemove", { clientX: 500, clientY: 28 }));
      });

      // paintCount should be > 0 before commit.
      expect(result.current.paintCount).toBeGreaterThan(0);
    });
  });
});
