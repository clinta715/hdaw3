import { useState, useCallback } from "react";
import { useProjectStore, nextTempId } from "../../store/projectStore";
import { useTransportStore } from "../../store/transportStore";
import { useUiStore } from "../../store/uiStore";
import { rpc } from "../../rpc";
import type { ContentItem } from "../PopUpBrowser";

interface ContextMenuItem {
  x: number;
  y: number;
  type: string;
  clip?: { clipId: number; trackIndex: number; isMidi: boolean };
  markerIndex?: number;
}

interface EmptyContextMenuItem {
  x: number;
  y: number;
  beat: number;
  trackIndex: number;
}

interface UseTimelineClipOpsOpts {
  clips: Array<{
    clipId: number;
    trackIndex: number;
    startBeat: number;
    durationBeats: number;
    isMidi: boolean;
    isGhost: boolean;
    ghostSourceId: number;
    name: string;
    sourceFile?: string;
  }>;
}

export function useTimelineClipOps(opts: UseTimelineClipOpsOpts) {
  const { clips } = opts;

  const [contextMenu, setContextMenu] = useState<ContextMenuItem | null>(null);
  const [emptyContextMenu, setEmptyContextMenu] = useState<EmptyContextMenuItem | null>(null);
  const [belowMenu, setBelowMenu] = useState<{ x: number; y: number } | null>(null);
  const [browseClipTarget, setBrowseClipTarget] = useState<{ trackIndex: number; beat: number } | null>(null);

  const handleContextMenu = useCallback(
    (e: React.MouseEvent, clip: (typeof clips)[0]) => {
      e.preventDefault();
      e.stopPropagation();
      const { selectedClipIds, selectClip } = useUiStore.getState();
      if (!selectedClipIds.has(clip.clipId)) {
        selectClip(clip.clipId, clip.trackIndex);
      }
      setContextMenu({ x: e.clientX, y: e.clientY, type: "clip", clip });
    },
    []
  );

  const handleMarkerContextMenu = useCallback(
    (e: React.MouseEvent, markerIndex: number) => {
      e.preventDefault();
      e.stopPropagation();
      setContextMenu({ x: e.clientX, y: e.clientY, type: "marker", markerIndex });
    },
    []
  );

  const handleCloseContextMenu = useCallback(() => {
    setContextMenu(null);
    setEmptyContextMenu(null);
    setBelowMenu(null);
  }, []);

  const handleBrowseClip = useCallback((trackIndex: number, beat: number) => {
    setBrowseClipTarget({ trackIndex, beat });
  }, []);

  const handleBrowseSelect = useCallback(
    (item: ContentItem) => {
      if (!browseClipTarget) return;
      const { trackIndex, beat } = browseClipTarget;
      const ext = "." + item.name.split(".").pop()?.toLowerCase();
      const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
      if (audioExts.includes(ext)) {
        rpc
          .call("project.addAudioClip", {
            trackIndex,
            start: Math.max(0, beat),
            duration: 4,
            sourceFile: item.path,
            name: item.name,
          })
          .catch(() => {});
      } else {
        rpc
          .call("project.addMidiClip", {
            trackIndex,
            start: Math.max(0, beat),
            duration: 4,
            name: item.name,
          })
          .catch(() => {});
      }
      useProjectStore.setState({ isDirty: true });
      setBrowseClipTarget(null);
    },
    [browseClipTarget]
  );

  const handleDeleteClip = useCallback(() => {
    const { selectedClipIds } = useUiStore.getState();
    const ids =
      selectedClipIds.size > 0
        ? [...selectedClipIds]
        : contextMenu?.clip
          ? [contextMenu.clip.clipId]
          : [];
    if (ids.length === 0) return;
    (async () => {
      try {
        await rpc.call("project.removeClips", { clipIds: ids });
        useUiStore.getState().clearSelection();
        useProjectStore.setState({ isDirty: true });
      } catch (e) {
        console.error("Failed to delete clips:", e);
      }
    })();
  }, [contextMenu]);

  const handleDuplicateClip = useCallback(() => {
    const { selectedClipIds } = useUiStore.getState();
    const snap = useProjectStore.getState().snapshot;
    if (!snap) return;
    const ids =
      selectedClipIds.size > 0
        ? [...selectedClipIds]
        : contextMenu?.clip
          ? [contextMenu.clip.clipId]
          : [];
    if (ids.length === 0) return;

    const tempIds: number[] = [];
    const clipIds: number[] = [];
    const newStarts: number[] = [];
    const newTrackIndices: number[] = [];
    for (const id of ids) {
      const src = snap.clips.find((c) => c.clipId === id);
      if (!src) continue;
      const targetStart = src.startBeat + src.durationBeats;
      const tempId = nextTempId();
      tempIds.push(tempId);
      clipIds.push(id);
      newStarts.push(targetStart);
      newTrackIndices.push(src.trackIndex);
      useProjectStore.getState().addPendingClip({ ...src, clipId: tempId, startBeat: targetStart });
    }
    if (clipIds.length === 0) return;

    setTimeout(
      () =>
        tempIds.forEach((t) => {
          if (useProjectStore.getState().pendingTempIds.has(t))
            useProjectStore.getState().removePending(t);
        }),
      1500
    );

    (async () => {
      try {
        const res = await rpc.call("project.duplicateClips", {
          clipIds,
          newStarts,
          newTrackIndices,
        });
        const newIds: number[] = Array.isArray(res) ? res : [];
        tempIds.forEach((tempId, i) => {
          const realId = newIds[i];
          if (typeof realId === "number" && realId > 0)
            useProjectStore.getState().resolvePending(tempId, realId);
          else useProjectStore.getState().removePending(tempId);
        });
        useProjectStore.setState({ isDirty: true });
      } catch {
        tempIds.forEach((t) => useProjectStore.getState().removePending(t));
      }
    })();
  }, [contextMenu]);

  const handleSplitClip = useCallback(() => {
    const { selectedClipIds } = useUiStore.getState();
    const ids =
      selectedClipIds.size > 0
        ? [...selectedClipIds]
        : contextMenu?.clip
          ? [contextMenu.clip.clipId]
          : [];
    if (ids.length === 0) return;
    (async () => {
      await rpc.call("project.sliceClipsAtPlayhead", { clipIds: ids }).catch(() => {});
      useProjectStore.setState({ isDirty: true });
    })();
  }, [contextMenu]);

  const pasteClipboard = useCallback(async () => {
    const { clipClipboard } = useUiStore.getState();
    if (clipClipboard.length === 0) return;
    const tr = useTransportStore.getState().transport;
    const playheadBeats = tr.currentTimeSeconds * (tr.bpm / 60);
    const minStart = Math.min(...clipClipboard.map((c) => c.startBeat));
    const starts = clipClipboard.map((c) => playheadBeats + (c.startBeat - minStart));
    const durations = clipClipboard.map((c) => c.durationBeats);
    const names = clipClipboard.map((c) => c.name);
    const sourceFiles = clipClipboard.map((c) =>
      c.isMidi ? "" : (c.sourceFile ?? "")
    );
    const trackIndex = clipClipboard[0]?.trackIndex ?? 0;
    await rpc.call("project.addClips", {
      trackIndex,
      starts,
      durations,
      names,
      sourceFiles,
    });
    useProjectStore.setState({ isDirty: true });
  }, []);

  return {
    contextMenu,
    setContextMenu,
    emptyContextMenu,
    setEmptyContextMenu,
    belowMenu,
    setBelowMenu,
    browseClipTarget,
    handleContextMenu,
    handleMarkerContextMenu,
    handleCloseContextMenu,
    handleBrowseClip,
    handleBrowseSelect,
    handleDeleteClip,
    handleDuplicateClip,
    handleSplitClip,
    pasteClipboard,
  };
}
