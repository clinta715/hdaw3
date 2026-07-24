import { useState, useRef, useCallback, useMemo } from "react";
import type { ClipSnapshot } from "../rpc/types";
import type { RpcClient } from "../rpc/client";
import type { DragState } from "../utils/timelineConstants";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "../components/snapUtils";

interface UseTimelineDragParams {
  clips: ClipSnapshot[];
  pps: number;
  TRACK_HEIGHT: number;
  tracksRef: React.RefObject<HTMLDivElement | null>;
  trackCount: number;
  rpc: RpcClient;
  engagementRef: React.MutableRefObject<"none" | "clip" | "rubber">;
}

// The C++ backend (FrontendRouter.cpp) returns BARE integers for
// duplicateClip / createGhostClip and a BARE array for paintClips — not an
// object like { clipId }. Older code parsed them as objects and the new-id
// capture silently never fired (root cause of the recurring ctrl/alt-drag
// "clip jumps back" bug). These helpers parse the real wire shapes and
// tolerate both forms defensively.
function parseBareInt(r: unknown): number {
  if (typeof r === "number" && Number.isFinite(r)) return r;
  if (r && typeof r === "object" && "clipId" in r) {
    const v = (r as { clipId?: unknown }).clipId;
    if (typeof v === "number" && Number.isFinite(v)) return v;
  }
  return -1;
}

function parseIdArray(r: unknown): number[] {
  if (Array.isArray(r)) return r.filter((v): v is number => typeof v === "number" && Number.isFinite(v));
  return [];
}

interface UseTimelineDragReturn {
  dragState: DragState | null;
  handleClipMouseDown: (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number, forcePaintRepeat?: boolean) => void;
  handleMouseMove: (e: globalThis.MouseEvent) => void;
  handleMouseUp: () => void;
  dragSelectedIdsRef: React.MutableRefObject<Set<number>>;
  dragCursor: string;
  dragPreviewStyle: React.CSSProperties | null;
  dragPreviewClip: ClipSnapshot | null;
  paintTiles: Array<{ left: number; width: number; top: number }>;
  paintCount: number;
}

export function useTimelineDrag({
  clips,
  pps,
  TRACK_HEIGHT,
  tracksRef,
  trackCount,
  rpc,
  engagementRef,
}: UseTimelineDragParams): UseTimelineDragReturn {
  const [dragState, setDragState] = useState<DragState | null>(null);
  const dragRef = useRef<DragState | null>(null);

  const updateDrag = useCallback((next: DragState | null) => {
    dragRef.current = next;
    setDragState(next);
  }, []);

  const dragSelectedIdsRef = useRef<Set<number>>(new Set());

  const handleMouseMoveRef = useRef<(e: globalThis.MouseEvent) => void>(() => {});
  const handleMouseUpRef = useRef<() => void>(() => {});

  const handleClipMouseDown = useCallback(
    (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number, forcePaintRepeat?: boolean) => {
      e.preventDefault();
      const el = e.currentTarget as HTMLElement;
      const r = el.getBoundingClientRect();
      const selected = useUiStore.getState().selectedClipIds;
      dragSelectedIdsRef.current = selected.has(clipId) ? new Set(selected) : new Set([clipId]);
      const paintRepeat = forcePaintRepeat || e.altKey ? true : undefined;
      let paintSpacing = 0;
      if (paintRepeat) {
        const clip = clips.find(c => c.clipId === clipId);
        if (clip) paintSpacing = clip.durationBeats;
      }
      const startClientX = e.clientX;
      const startClientY = e.clientY;
      let engaged = false;
      const pendingDrag: DragState = {
        clipId, startTrackIndex: trackIndex, startBeat,
        offsetX: e.clientX - r.left, offsetY: e.clientY - r.top,
        mouseX: e.clientX, mouseY: e.clientY,
        isDuplicate: e.ctrlKey || e.metaKey ? true : undefined,
        isGhostClone: (e.ctrlKey || e.metaKey) && e.shiftKey ? true : undefined,
        paintRepeat,
        paintOriginBeat: startBeat,
        paintSpacing,
        paintedClipIds: [],
      };

      const onMove = (ev: globalThis.MouseEvent) => {
        if (!engaged) {
          if (engagementRef.current === "rubber") {
            window.removeEventListener("mousemove", onMove);
            window.removeEventListener("mouseup", onUp);
            return;
          }
          const dx = ev.clientX - startClientX;
          const dy = ev.clientY - startClientY;
          if (dx * dx + dy * dy < 16) return;
          engaged = true;
          engagementRef.current = "clip";
          updateDrag(pendingDrag);
        }
        handleMouseMoveRef.current(ev);
      };
      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        engagementRef.current = "none";
        if (engaged) {
          handleMouseUpRef.current();
        }
      };
      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [updateDrag, clips, engagementRef]
  );

  // During the drag we only track the mouse; all copy/paint/ghost work is
  // committed once on mouseup. This eliminates the timing race where a fast
  // release raced the eager async-duplicate and targeted the wrong clip ids.
  const handleMouseMove = useCallback((e: globalThis.MouseEvent) => {
    const d = dragRef.current;
    if (!d) return;
    updateDrag({ ...d, mouseX: e.clientX, mouseY: e.clientY });
  }, [updateDrag]);

  const handleMouseUp = useCallback(() => {
    const d = dragRef.current;
    if (!d) return;
    const el = tracksRef.current;
    updateDrag(null);
    if (!el) return;
    const cr = el.getBoundingClientRect();

    // ── Paint repeat: commit once via the atomic project.paintClips RPC.
    // During the drag we only showed pending preview tiles + the +N badge; no
    // clips were created until release. paintClips wraps its own transaction.
    if (d.paintRepeat) {
      if (d.paintSpacing <= 0) return;
      const scroll = el.scrollLeft;
      const mouseBeat = Math.max(0, (d.mouseX - cr.left + scroll) / pps);
      const count = Math.max(0, Math.floor((mouseBeat - d.paintOriginBeat) / d.paintSpacing));
      if (count <= 0) return;
      const sourceClipIds = [...dragSelectedIdsRef.current];
      (async () => {
        const res = await rpc.call("project.paintClips", {
          sourceClipIds,
          originBeat: d.paintOriginBeat,
          spacing: d.paintSpacing,
          targetTrackIndex: d.startTrackIndex,
          count,
        }).catch(() => null);
        const newIds = parseIdArray(res);
        if (newIds.length > 0) {
          useUiStore.setState({ selectedClipIds: new Set(newIds) });
          dragSelectedIdsRef.current = new Set(newIds);
        }
        // The backend broadcasts notify.treeChanged ~16ms after the ValueTree
        // mutation (FrontendTreeWatcher), and main.tsx already reconciles the
        // full snapshot on that push — so an explicit syncSnapshot here would
        // only block on work the push does anyway. Mark dirty optimistically
        // (the only consumers are a UI dot + a beforeunload guard).
        useProjectStore.setState({ isDirty: true });
      })();
      return;
    }

    const relX = d.mouseX - cr.left;
    const relY = d.mouseY - cr.top;
    const rawStart = Math.max(0, (relX - d.offsetX) / pps);
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const newStart = snapEnabled ? snapToGrid(rawStart, snapDivision) : rawStart;
    const newTrackIndex = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), trackCount - 1);
    const deltaStart = newStart - d.startBeat;
    const deltaTrack = newTrackIndex - d.startTrackIndex;

    // No meaningful move and not a copy → nothing to commit.
    const isCopy = !!d.isDuplicate || !!d.isGhostClone;
    if (!isCopy && newTrackIndex === d.startTrackIndex && Math.abs(deltaStart) <= 0.01) return;

    // ── Copy modes (ctrl+drag duplicate, ctrl+shift+drag ghost clone):
    // duplicate/ghost on release, then move the NEW ids to the drop target,
    // all in one transaction. Reads clip data from the store (pitfall #1) and
    // parses the backend's bare-int return values (root cause #1).
    if (isCopy) {
      const origIds = [...dragSelectedIdsRef.current];
      const currentClips = useProjectStore.getState().snapshot?.clips ?? clips;
      const clipIds: number[] = [];
      const newStarts: number[] = [];
      const newTrackIndices: number[] = [];
      for (const id of origIds) {
        const orig = currentClips.find(c => c.clipId === id) ?? clips.find(c => c.clipId === id);
        if (!orig) continue;
        const offset = orig.startBeat - d.startBeat;
        clipIds.push(id);
        newStarts.push(Math.max(0, newStart + offset));
        newTrackIndices.push(Math.min(Math.max(0, orig.trackIndex + deltaTrack), trackCount - 1));
      }
      if (clipIds.length === 0) return;
      (async () => {
        const res = await rpc.call("project.duplicateClips", {
          clipIds, newStarts, newTrackIndices,
        }).catch(() => null);
        const newIds = parseIdArray(res);
        if (newIds.length > 0) {
          useUiStore.setState({ selectedClipIds: new Set(newIds) });
          dragSelectedIdsRef.current = new Set(newIds);
        }
        useProjectStore.setState({ isDirty: true });
      })();
      return;
    }

    // ── Normal move: optimistic placement + batch RPC.
    const ids = [...dragSelectedIdsRef.current];
    const maxTrack = trackCount - 1;
    const currentClips = useProjectStore.getState().snapshot?.clips ?? clips;
    const moveIds: number[] = [];
    const moveStarts: number[] = [];
    const moveTracks: number[] = [];
    for (const id of ids) {
      const clip = currentClips.find(c => c.clipId === id);
      if (!clip) continue;
      moveIds.push(id);
      moveStarts.push(Math.max(0, clip.startBeat + deltaStart));
      moveTracks.push(Math.min(Math.max(0, clip.trackIndex + deltaTrack), maxTrack));
    }

    useProjectStore.setState((s) => {
      if (!s.snapshot) return {};
      const movedSet = new Set(ids);
      return {
        snapshot: {
          ...s.snapshot,
          clips: s.snapshot.clips.map((c) =>
            movedSet.has(c.clipId)
              ? {
                  ...c,
                  startBeat: Math.max(0, c.startBeat + deltaStart),
                  trackIndex: Math.min(Math.max(0, c.trackIndex + deltaTrack), maxTrack),
                }
              : c
          ),
        },
      };
    });

    if (moveIds.length > 0) {
      (async () => {
        await rpc.call("project.moveClips", { clipIds: moveIds, newStarts: moveStarts, newTrackIndices: moveTracks }).catch(() => {});
        useProjectStore.setState({ isDirty: true });
      })();
    }
  }, [pps, trackCount, clips, updateDrag, tracksRef, rpc, TRACK_HEIGHT]);

  handleMouseMoveRef.current = handleMouseMove;
  handleMouseUpRef.current = handleMouseUp;

  const dragCursor = dragState
    ? dragState.paintRepeat
      ? "crosshair"
      : dragState.isGhostClone
        ? "alias"
        : dragState.isDuplicate
          ? "copy"
          : "grabbing"
    : "";

  const { dragPreviewStyle, dragPreviewClip } = useMemo(() => {
    if (!dragState || dragState.paintRepeat) return { dragPreviewStyle: null, dragPreviewClip: null };
    const el = tracksRef.current;
    if (!el) return { dragPreviewStyle: null, dragPreviewClip: null };
    const cr = el.getBoundingClientRect();
    const relX = dragState.mouseX - cr.left;
    const relY = dragState.mouseY - cr.top;
    const rawStart = Math.max(0, (relX - dragState.offsetX) / pps);
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const gs = snapEnabled ? snapToGrid(rawStart, snapDivision) : rawStart;
    const gi = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), trackCount - 1);
    const orig = clips.find((c) => c.clipId === dragState.clipId);
    if (!orig) return { dragPreviewStyle: null, dragPreviewClip: null };
    return {
      dragPreviewStyle: { left: gs * pps, width: Math.max(4, orig.durationBeats * pps), height: TRACK_HEIGHT - 8, top: gi * TRACK_HEIGHT + 4 } as React.CSSProperties,
      dragPreviewClip: orig,
    };
  }, [dragState, pps, TRACK_HEIGHT, trackCount, clips, tracksRef]);

  const paintTiles = useMemo(() => {
    const tiles: { left: number; width: number; top: number }[] = [];
    if (!dragState?.paintRepeat || dragState.paintSpacing <= 0) return tiles;
    const el = tracksRef.current;
    if (!el) return tiles;
    const cr = el.getBoundingClientRect();
    const mouseBeat = Math.max(0, (dragState.mouseX - cr.left + el.scrollLeft) / pps);
    const desiredCount = Math.max(0, Math.floor((mouseBeat - dragState.paintOriginBeat) / dragState.paintSpacing));
    const orig = clips.find((c) => c.clipId === dragState.clipId);
    const trackTop = dragState.startTrackIndex * TRACK_HEIGHT;
    if (!orig) return tiles;
    const tileW = Math.max(4, orig.durationBeats * pps);
    // Tiles now represent pending previews only (clips are committed once on
    // release), so all desiredCount+1 tiles are uncommitted.
    for (let i = 0; i <= desiredCount; i++) {
      const tileBeat = dragState.paintOriginBeat + i * dragState.paintSpacing;
      tiles.push({ left: tileBeat * pps, width: tileW, top: trackTop + 4 });
    }
    return tiles;
  }, [dragState, pps, TRACK_HEIGHT, clips, tracksRef]);

  // Intended number of copies (excludes the original). Drives the +N badge;
  // mirrors the count computed in the paintRepeat branch of handleMouseUp.
  const paintCount = useMemo(() => {
    if (!dragState?.paintRepeat || dragState.paintSpacing <= 0) return 0;
    const el = tracksRef.current;
    if (!el) return 0;
    const cr = el.getBoundingClientRect();
    const mouseBeat = Math.max(0, (dragState.mouseX - cr.left + el.scrollLeft) / pps);
    return Math.max(0, Math.floor((mouseBeat - dragState.paintOriginBeat) / dragState.paintSpacing));
  }, [dragState, pps, tracksRef]);

  return {
    dragState,
    handleClipMouseDown,
    handleMouseMove,
    handleMouseUp,
    dragSelectedIdsRef,
    dragCursor,
    dragPreviewStyle,
    dragPreviewClip,
    paintTiles,
    paintCount,
  };
}
