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
}

export function useTimelineDrag({
  clips,
  pps,
  TRACK_HEIGHT,
  tracksRef,
  trackCount,
  rpc,
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
      updateDrag({
        clipId, startTrackIndex: trackIndex, startBeat,
        offsetX: e.clientX - r.left, offsetY: e.clientY - r.top,
        mouseX: e.clientX, mouseY: e.clientY,
        isDuplicate: e.ctrlKey || e.metaKey ? true : undefined,
        isGhostClone: (e.ctrlKey || e.metaKey) && e.shiftKey ? true : undefined,
        paintRepeat,
        paintOriginBeat: startBeat,
        paintSpacing,
        paintedClipIds: [],
      });

      const onMove = (ev: globalThis.MouseEvent) => handleMouseMoveRef.current(ev);
      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        handleMouseUpRef.current();
      };
      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [updateDrag, clips]
  );

  const handleMouseMove = useCallback((e: globalThis.MouseEvent) => {
    const d = dragRef.current;
    if (!d) return;

    if (d.paintRepeat && d.paintSpacing > 0) {
      const el = tracksRef.current;
      if (!el) { updateDrag({ ...d, mouseX: e.clientX, mouseY: e.clientY }); return; }
      const rect = el.getBoundingClientRect();
      const scroll = el.scrollLeft;
      const mouseBeat = Math.max(0, (e.clientX - rect.left + scroll) / pps);
      const desiredCount = Math.max(0, Math.floor((mouseBeat - d.paintOriginBeat) / d.paintSpacing));
      const currentCount = d.paintedClipIds.length;

      if (desiredCount > currentCount) {
        const ids = dragSelectedIdsRef.current;
        (async () => {
          for (let i = currentCount; i < desiredCount; i++) {
            const newStart = d.paintOriginBeat + (i + 1) * d.paintSpacing;
            for (const id of ids) {
              const clip = clips.find(c => c.clipId === id);
              if (!clip) continue;
              const r = await rpc.call("project.duplicateClip", { clipId: id }).catch(() => null);
              if (r && typeof r === "object" && "clipId" in r) {
                const newId = (r as { clipId: number }).clipId;
                const offset = clip.startBeat - d.paintOriginBeat;
                await rpc.call("project.moveClipWithOverlap", {
                  clipId: newId, newTrackIndex: d.startTrackIndex, newStart: newStart + offset
                }).catch(() => {});
                d.paintedClipIds.push(newId);
              }
            }
          }
          await useProjectStore.getState().syncDirtyFlag(rpc);
          await useProjectStore.getState().syncSnapshot(rpc);
        })();
      } else if (desiredCount < currentCount) {
        const toRemove = d.paintedClipIds.splice(desiredCount);
        (async () => {
          for (const id of toRemove) {
            await rpc.call("project.removeClip", { clipId: id }).catch(() => {});
          }
          await useProjectStore.getState().syncDirtyFlag(rpc);
          await useProjectStore.getState().syncSnapshot(rpc);
        })();
      }

      updateDrag({ ...d, mouseX: e.clientX, mouseY: e.clientY });
      return;
    }

    if (d.isGhostClone && !d.ghostDuplicated) {
      const { snapshot } = useProjectStore.getState();
      if (!snapshot) return;
      const ids = dragSelectedIdsRef.current;
      const newIds = new Set<number>();
      dragRef.current = { ...d, ghostDuplicated: true };
      setDragState(dragRef.current);
      (async () => {
        await rpc.call("project.beginTransaction", { name: "ghost-clone clips" });
        const el = tracksRef.current;
        if (!el) return;
        const rect = el.getBoundingClientRect();
        const relY = e.clientY - rect.top;
        const newTrackIdx = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), trackCount - 1);
        const relX = e.clientX - rect.left + el.scrollLeft;
        const rawStart = Math.max(0, (relX - d.offsetX) / pps);
        for (const id of ids) {
          const clip = clips.find(c => c.clipId === id);
          if (!clip) continue;
          const offset = clip.startBeat - d.startBeat;
          const r = await rpc.call("project.createGhostClip", {
            sourceClipId: id, newStart: Math.max(0, rawStart + offset), newTrackIndex: newTrackIdx
          }).catch(() => null);
          if (r && typeof r === "object" && "clipId" in r) newIds.add((r as { clipId: number }).clipId);
        }
        await rpc.call("project.endTransaction");
        if (newIds.size > 0) {
          useUiStore.setState({ selectedClipIds: newIds });
          dragSelectedIdsRef.current = newIds;
          const first = [...newIds][0];
          dragRef.current = { ...d, clipId: first, ghostDuplicated: true };
          setDragState(dragRef.current);
        }
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      })();
      return;
    }

    if (d.isDuplicate && !d.duplicated) {
      const { snapshot } = useProjectStore.getState();
      if (!snapshot) return;
      const ids = dragSelectedIdsRef.current;
      const newIds = new Set<number>();
      dragRef.current = { ...d, duplicated: true };
      setDragState(dragRef.current);
      (async () => {
        await rpc.call("project.beginTransaction", { name: "duplicate clips" });
        for (const id of ids) {
          const r = await rpc.call("project.duplicateClip", { clipId: id }).catch(() => null);
          if (r && typeof r === "object" && "clipId" in r) newIds.add((r as { clipId: number }).clipId);
        }
        await rpc.call("project.endTransaction");
        if (newIds.size > 0) {
          useUiStore.setState({ selectedClipIds: newIds });
          dragSelectedIdsRef.current = newIds;
          const first = [...newIds][0];
          dragRef.current = { ...d, clipId: first, duplicated: true };
          setDragState(dragRef.current);
        }
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      })();
      return;
    }
    updateDrag({ ...d, mouseX: e.clientX, mouseY: e.clientY });
  }, [updateDrag, pps, trackCount, clips, tracksRef, rpc, TRACK_HEIGHT]);

  const handleMouseUp = useCallback(() => {
    const d = dragRef.current;
    if (!d) return;
    if (d.paintRepeat) {
      d.paintedClipIds = [];
      updateDrag(null);
      return;
    }
    const el = tracksRef.current;
    updateDrag(null);
    if (!el) return;
    const cr = el.getBoundingClientRect();
    const relX = d.mouseX - cr.left;
    const relY = d.mouseY - cr.top;
    const rawStart = Math.max(0, (relX - d.offsetX) / pps);
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const newStart = snapEnabled ? snapToGrid(rawStart, snapDivision) : rawStart;
    const newTrackIndex = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), trackCount - 1);
    if (newTrackIndex !== d.startTrackIndex || Math.abs(newStart - d.startBeat) > 0.01) {
      const deltaStart = newStart - d.startBeat;
      const deltaTrack = newTrackIndex - d.startTrackIndex;
      const ids = dragSelectedIdsRef.current;

      const maxTrack = trackCount - 1;
      useProjectStore.setState((s) => {
        if (!s.snapshot) return {};
        const movedSet = ids;
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

      (async () => {
        await rpc.call("project.beginTransaction", { name: "move clips" });
        for (const id of ids) {
          const clip = clips.find(c => c.clipId === id);
          if (!clip) continue;
          const clipNewStart = Math.max(0, clip.startBeat + deltaStart);
          const clipNewTrack = Math.min(Math.max(0, clip.trackIndex + deltaTrack), trackCount - 1);
          await rpc.call("project.moveClipWithOverlap", { clipId: id, newTrackIndex: clipNewTrack, newStart: clipNewStart }).catch(() => {});
        }
        await rpc.call("project.endTransaction");
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
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
    for (let i = 0; i <= desiredCount; i++) {
      const tileBeat = dragState.paintOriginBeat + i * dragState.paintSpacing;
      const isCommitted = i < dragState.paintedClipIds.length;
      if (!isCommitted) {
        tiles.push({ left: tileBeat * pps, width: tileW, top: trackTop + 4 });
      }
    }
    return tiles;
  }, [dragState, pps, TRACK_HEIGHT, clips, tracksRef]);

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
  };
}
