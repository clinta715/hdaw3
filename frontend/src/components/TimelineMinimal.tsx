import { useState, useMemo, useRef, useCallback, useEffect } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useMarkerStore } from "../store/markerStore";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { WaveformCanvas } from "./WaveformCanvas";
import { snapToGrid } from "./snapUtils";
import { useTimelineDrag } from "../hooks/useTimelineDrag";
import "./TimelineMinimal.css";

function computeRubberBandSelection(
  rb: { x1: number; y1: number; x2: number; y2: number },
  clips: ReadonlyArray<{ clipId: number; startBeat: number; durationBeats: number; trackIndex: number }>,
  pps: number
): Set<number> {
  const minX = Math.min(rb.x1, rb.x2);
  const maxX = Math.max(rb.x1, rb.x2);
  const minY = Math.min(rb.y1, rb.y2);
  const maxY = Math.max(rb.y1, rb.y2);
  const selected = new Set<number>();
  for (const clip of clips) {
    const cx = clip.startBeat * pps;
    const cy = clip.trackIndex * TRACK_HEIGHT;
    const cw = clip.durationBeats * pps;
    const ch = TRACK_HEIGHT;
    if (cx + cw >= minX && cx <= maxX && cy + ch >= minY && cy <= maxY) {
      selected.add(clip.clipId);
    }
  }
  return selected;
}

const DEFAULT_PPS = 40;
const MIN_PPS = 10;
const MAX_PPS = 200;
const TRACK_HEIGHT = 56;
const RULER_HEIGHT = 24;

interface TrimState {
  clipId: number;
  side: "left" | "right";
  initialStartBeat: number;
  initialDuration: number;
  currentStartBeat: number;
  currentDuration: number;
}

export default function TimelineMinimal() {
  const [pps, setPps] = useState(DEFAULT_PPS);

  const snapshot = useProjectStore((s) => s.snapshot);
  const transport = useTransportStore((s) => s.transport);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const markers = useMarkerStore((s) => s.markers);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];

  const rulerRef = useRef<HTMLDivElement>(null);
  const tracksRef = useRef<HTMLDivElement>(null);

  // --- Clip drag (extracted hook) ---
  const {
    dragState,
    handleClipMouseDown,
    dragSelectedIdsRef,
    dragCursor,
    dragPreviewStyle,
    dragPreviewClip,
    paintTiles,
  } = useTimelineDrag({
    clips,
    pps,
    TRACK_HEIGHT,
    tracksRef,
    trackCount: tracks.length,
    rpc,
  });

  // --- Trim state ---
  const [trimState, setTrimState] = useState<TrimState | null>(null);
  const trimRef = useRef<TrimState | null>(null);
  const updateTrim = useCallback((next: TrimState | null) => {
    trimRef.current = next;
    setTrimState(next);
  }, []);

  // --- Fade drag state ---
  const [fadeDrag, setFadeDrag] = useState<{ clipId: number; side: "in" | "out"; initialValue: number; startBeat: number; durationBeats: number } | null>(null);
  const fadeDragRef = useRef(fadeDrag);
  fadeDragRef.current = fadeDrag;

  // --- Loop drag state ---
  const [loopDrag, setLoopDrag] = useState<"start" | "end" | null>(null);
  const [dragBeat, setDragBeat] = useState(0);
  const dragBeatRef = useRef(0);

  // --- Rubber band state ---
  const [rubberBand, setRubberBand] = useState<{ x1: number; y1: number; x2: number; y2: number } | null>(null);
  const rubberBandRef = useRef(rubberBand);
  rubberBandRef.current = rubberBand;
  const rubberBandJustCompleted = useRef(false);

  // --- Context menu ---
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; type: string; clip?: typeof clips[0]; markerIndex?: number } | null>(null);
  const [emptyContextMenu, setEmptyContextMenu] = useState<{ x: number; y: number; beat: number } | null>(null);

  useEffect(() => {
    const close = (e: MouseEvent) => {
      // Don't close if clicking inside context menu
      const target = e.target as HTMLElement;
      if (target.closest('.clip-context-menu')) {
        return;
      }
      setContextMenu(null);
      setEmptyContextMenu(null);
    };
    // Use mousedown instead of click to avoid conflicts with button onClick
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, []);

  // --- Group clips by track ---
  const clipsByTrack = useMemo(() => {
    const map = new Map<number, typeof clips>();
    for (const c of clips) {
      const group = map.get(c.trackIndex) ?? [];
      group.push(c);
      map.set(c.trackIndex, group);
    }
    return map;
  }, [clips]);

  // --- Dimensions ---
  const maxEnd = clips.reduce((max, c) => Math.max(max, c.startBeat + c.durationBeats), 4);
  const totalW = Math.max(maxEnd * pps, 800);
  const totalH = tracks.length * TRACK_HEIGHT;

  // --- Playhead ---
  const playheadBeats = transport.currentTimeSeconds * (transport.bpm / 60);
  const playheadX = playheadBeats * pps;

  // --- Ruler markers ---
  const rulerMarkers = useMemo(() => {
    const markers: { beat: number; isBar: boolean }[] = [];
    const numBeats = Math.ceil(totalW / pps) + 1;
    for (let b = 0; b <= numBeats; b++) {
      markers.push({ beat: b, isBar: b % 4 === 0 });
    }
    return markers;
  }, [totalW, pps]);

  // --- Loop positions ---
  const showLoop = transport.isLooping;
  const dispLoopStart = loopDrag === "start" ? dragBeat : transport.loopStart;
  const dispLoopEnd = loopDrag === "end" ? dragBeat : transport.loopEnd;
  const loopLX = Math.max(0, dispLoopStart) * pps;
  const loopRX = Math.max(loopLX / pps + 0.25, dispLoopEnd) * pps;

  // --- Scroll sync ---
  const onTracksScroll = useCallback(() => {
    if (rulerRef.current && tracksRef.current) {
      rulerRef.current.scrollLeft = tracksRef.current.scrollLeft;
    }
  }, []);

  // --- Zoom ---
  const zoomIn = useCallback(() => setPps((p) => Math.min(MAX_PPS, p * 1.25)), []);
  const zoomOut = useCallback(() => setPps((p) => Math.max(MIN_PPS, p / 1.25)), []);
  const zoomFit = useCallback(() => {
    if (maxEnd <= 0) { setPps(DEFAULT_PPS); return; }
    const cw = tracksRef.current?.clientWidth ?? 800;
    setPps(Math.round(Math.min(MAX_PPS, Math.max(MIN_PPS, cw / maxEnd))));
  }, [maxEnd]);

  const onWheel = useCallback((e: React.WheelEvent) => {
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault();
    setPps((p) => {
      const factor = e.deltaY < 0 ? 1.25 : 0.8;
      return Math.min(MAX_PPS, Math.max(MIN_PPS, p * factor));
    });
  }, []);

  // --- Ruler click-to-seek / drag-scrub ---
  const [isScrubbing, setIsScrubbing] = useState(false);
  const scrubRef = useRef(false);

  const beatToSec = useCallback((beat: number) => beat * 60 / transport.bpm, [transport.bpm]);

  const handleRulerMouseDown = useCallback((e: React.MouseEvent) => {
    const rect = tracksRef.current?.getBoundingClientRect();
    if (!rect) return;
    const scroll = tracksRef.current?.scrollLeft ?? 0;
    const beat = Math.max(0, (e.clientX - rect.left + scroll) / pps);

    // Ctrl+click = set loop start, Alt+click = set loop end
    if (e.ctrlKey || e.metaKey) {
      rpc.call("project.setLoopStart", { beat }).catch(() => {});
      const t = useTransportStore.getState().transport;
      useTransportStore.getState().update({ ...t, loopStart: beat });
      // Auto-enable looping if not already on
      if (!t.isLooping) rpc.call("transport.toggleLoop").catch(() => {});
      return;
    }
    if (e.altKey) {
      rpc.call("project.setLoopEnd", { beat }).catch(() => {});
      const t = useTransportStore.getState().transport;
      useTransportStore.getState().update({ ...t, loopEnd: beat });
      if (!t.isLooping) rpc.call("transport.toggleLoop").catch(() => {});
      return;
    }

    rpc.call("transport.seekToSeconds", { seconds: beatToSec(beat) }).catch(() => {});
    scrubRef.current = true;
    setIsScrubbing(true);

    const onMove = (ev: globalThis.MouseEvent) => {
      if (!scrubRef.current) return;
      const r = tracksRef.current?.getBoundingClientRect();
      if (!r) return;
      const s = tracksRef.current?.scrollLeft ?? 0;
      const b = Math.max(0, (ev.clientX - r.left + s) / pps);
      rpc.call("transport.seekToSeconds", { seconds: beatToSec(b) }).catch(() => {});
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      scrubRef.current = false;
      setIsScrubbing(false);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps, transport.bpm, beatToSec]);

  // --- Loop drag handlers ---
  const startLoopDrag = useCallback((which: "start" | "end") => (e: React.MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    setLoopDrag(which);
    setDragBeat(which === "start" ? transport.loopStart : transport.loopEnd);

    const onMove = (ev: globalThis.MouseEvent) => {
      const rect = tracksRef.current?.getBoundingClientRect();
      if (!rect) return;
      const scroll = tracksRef.current?.scrollLeft ?? 0;
      const beat = Math.max(0, (ev.clientX - rect.left + scroll) / pps);
      dragBeatRef.current = beat;
      setDragBeat(beat);
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const finalBeat = dragBeatRef.current;
      // Optimistic local update: patch the transport store's loop bound before
      // clearing the drag preview, so the loop band doesn't snap back to its
      // old position for the round-trip until the backend push arrives.
      const t = useTransportStore.getState().transport;
      useTransportStore.getState().update({
        ...t,
        loopStart: which === "start" ? finalBeat : t.loopStart,
        loopEnd: which === "end" ? finalBeat : t.loopEnd,
      });
      setLoopDrag(null);
      const method = which === "start" ? "project.setLoopStart" : "project.setLoopEnd";
      rpc.call(method, which === "start" ? { beat: finalBeat } : { beat: finalBeat }).catch(() => {});
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps, transport.loopStart, transport.loopEnd, dragBeat]);

  // --- Rubber band handler ---
  const handleRubberBandStart = useCallback((e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest(".tl-clip")) return;
    rubberBandJustCompleted.current = false;
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left + el.scrollLeft;
    const y = e.clientY - rect.top + el.scrollTop;
    setRubberBand({ x1: x, y1: y, x2: x, y2: y });

    const onMove = (ev: globalThis.MouseEvent) => {
      const r = el.getBoundingClientRect();
      const newX2 = ev.clientX - r.left + el.scrollLeft;
      const newY2 = ev.clientY - r.top + el.scrollTop;
      setRubberBand(prev => prev ? {
        ...prev,
        x2: newX2,
        y2: newY2,
      } : null);
      const rb = rubberBandRef.current;
      if (rb) {
        useUiStore.setState({ selectedClipIds: computeRubberBandSelection(
          { x1: rb.x1, y1: rb.y1, x2: newX2, y2: newY2 }, clips, pps) });
      }
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const rb = rubberBandRef.current;
      if (rb) {
        const selected = computeRubberBandSelection(rb, clips, pps);
        if (selected.size > 0) {
          useUiStore.setState({ selectedClipIds: selected });
          rubberBandJustCompleted.current = true;
        }
      }
      setRubberBand(null);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [clips, pps]);

  const handleTrimStart = useCallback(
    (e: React.MouseEvent, clip: typeof clips[0], side: "left" | "right") => {
      e.stopPropagation();
      e.preventDefault();
      updateTrim({
        clipId: clip.clipId,
        side,
        initialStartBeat: clip.startBeat,
        initialDuration: clip.durationBeats,
        currentStartBeat: clip.startBeat,
        currentDuration: clip.durationBeats,
      });

      const onMove = (ev: globalThis.MouseEvent) => {
        const d = trimRef.current;
        if (!d) return;
        const el = tracksRef.current;
        if (!el) return;
        const rect = el.getBoundingClientRect();
        const scroll = el.scrollLeft;
        const rawMouseBeat = (ev.clientX - rect.left + scroll) / pps;
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const mouseBeat = snapEnabled ? snapToGrid(rawMouseBeat, snapDivision) : rawMouseBeat;

        if (d.side === "left") {
          const maxStart = d.initialStartBeat + d.initialDuration - 0.5;
          const newStart = Math.max(0, Math.min(mouseBeat, maxStart));
          const newDuration = d.initialDuration + (d.initialStartBeat - newStart);
          updateTrim({ ...d, currentStartBeat: newStart, currentDuration: newDuration });
        } else {
          const newDuration = Math.max(0.5, mouseBeat - d.initialStartBeat);
          updateTrim({ ...d, currentDuration: newDuration });
        }
      };

      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        const d = trimRef.current;
        if (!d) return;

        const changed = d.side === "left"
          ? Math.abs(d.currentStartBeat - d.initialStartBeat) > 0.01
          : Math.abs(d.currentDuration - d.initialDuration) > 0.01;

        // Optimistic local update: apply the committed trim to the snapshot
        // BEFORE clearing the preview state, so the clip doesn't snap back to
        // its pre-trim bounds for the 50-150ms until syncSnapshot returns.
        if (changed) {
          useProjectStore.setState((s) => {
            if (!s.snapshot) return {};
            return {
              snapshot: {
                ...s.snapshot,
                clips: s.snapshot.clips.map((c) =>
                  c.clipId === d.clipId
                    ? { ...c, startBeat: d.currentStartBeat, durationBeats: d.currentDuration }
                    : c
                ),
              },
            };
          });
        }

        updateTrim(null);

        if (changed) {
          if (d.side === "left") {
            (async () => {
              try {
                await rpc.call("project.beginTransaction", { name: "trim clip" });
                await rpc.call("project.setClipStart", { clipId: d.clipId, start: d.currentStartBeat });
                await rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration });
                await rpc.call("project.endTransaction");
                await useProjectStore.getState().syncDirtyFlag(rpc);
                await useProjectStore.getState().syncSnapshot(rpc);
              } catch (e) { console.error("trim failed", e); }
            })();
          } else {
            (async () => {
              await rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration }).catch(() => {});
              await useProjectStore.getState().syncDirtyFlag(rpc);
              await useProjectStore.getState().syncSnapshot(rpc);
            })();
          }
        }
      };

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [pps, updateTrim]
  );

  const handleFadeStart = useCallback((e: React.MouseEvent, clip: typeof clips[0], side: "in" | "out") => {
    e.stopPropagation();
    e.preventDefault();
    setFadeDrag({
      clipId: clip.clipId,
      side,
      initialValue: side === "in" ? clip.fadeIn : clip.fadeOut,
      startBeat: clip.startBeat,
      durationBeats: clip.durationBeats,
    });

    const onMove = (ev: globalThis.MouseEvent) => {
      const el = tracksRef.current;
      if (!el) return;
      const rect = el.getBoundingClientRect();
      const scroll = el.scrollLeft;
      const d = fadeDragRef.current;
      if (!d) return;
      const clipStartPx = d.startBeat * pps;
      const clipEndPx = (d.startBeat + d.durationBeats) * pps;
      const mousePx = ev.clientX - rect.left + scroll;
      if (d.side === "in") {
        const newFade = Math.max(0, Math.min(d.durationBeats / 2, (mousePx - clipStartPx) / pps));
        setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
      } else {
        const newFade = Math.max(0, Math.min(d.durationBeats / 2, (clipEndPx - mousePx) / pps));
        setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
      }
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const d = fadeDragRef.current;
      if (d) {
        // Optimistic local update: write the committed fade to the snapshot
        // BEFORE clearing the preview, so the fade triangle doesn't snap back
        // to its old shape for the round-trip until syncSnapshot returns.
        const fadedClip = d.side === "in" ? d.initialValue : undefined;
        const fadedOut = d.side === "out" ? d.initialValue : undefined;
        useProjectStore.setState((s) => {
          if (!s.snapshot) return {};
          return {
            snapshot: {
              ...s.snapshot,
              clips: s.snapshot.clips.map((c) =>
                c.clipId === d.clipId
                  ? {
                      ...c,
                      fadeIn: fadedClip !== undefined ? fadedClip : c.fadeIn,
                      fadeOut: fadedOut !== undefined ? fadedOut : c.fadeOut,
                    }
                  : c
              ),
            },
          };
        });

        const method = d.side === "in" ? "project.setClipFadeIn" : "project.setClipFadeOut";
        rpc.call(method, { clipId: d.clipId, [d.side === "in" ? "fadeIn" : "fadeOut"]: d.initialValue }).then(() => {
          useProjectStore.getState().syncDirtyFlag(rpc);
          useProjectStore.getState().syncSnapshot(rpc);
        }).catch(() => {});
      }
      setFadeDrag(null);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps]);

  // --- File drag-and-drop import ---
  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();

    // Check for internal file browser drag
    const hdawData = e.dataTransfer.getData("application/hdaw-file");
    if (hdawData) {
      try {
        const { path: filePath, name: fileName } = JSON.parse(hdawData);
        const rect = e.currentTarget.getBoundingClientRect();
        const y = e.clientY - rect.top + (tracksRef.current?.scrollTop ?? 0);
        const trackIdx = Math.floor(y / TRACK_HEIGHT);
        const elScroll = tracksRef.current?.scrollLeft ?? 0;
        const beatX = (e.clientX - rect.left + elScroll) / pps;
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const startBeat = snapEnabled ? Math.round(beatX / snapDivision) * snapDivision : beatX;

        const ext = "." + fileName.split(".").pop()?.toLowerCase();
        const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
        const midiExts = [".mid", ".midi"];

        if (audioExts.includes(ext)) {
          rpc.call("project.addAudioClip", {
            trackIndex: Math.max(0, trackIdx),
            start: Math.max(0, startBeat),
            duration: 4,
            sourceFile: filePath,
            name: fileName,
          }).catch(() => {});
        } else if (midiExts.includes(ext)) {
          rpc.call("project.addMidiClip", {
            trackIndex: Math.max(0, trackIdx),
            start: Math.max(0, startBeat),
            duration: 4,
            name: fileName,
          }).catch(() => {});
        }
        useProjectStore.getState().syncSnapshot(rpc);
      } catch {}
      return;
    }

    // External file drop (from OS)
    const files = Array.from(e.dataTransfer.files);
    const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
    const tr = useTransportStore.getState().transport;
    for (const file of files) {
      const ext = "." + file.name.split(".").pop()?.toLowerCase();
      if (audioExts.includes(ext)) {
        const startBeat = tr.currentTimeSeconds * (tr.bpm / 60);
        rpc.call("project.addAudioClip", {
          trackIndex: 0,
          start: startBeat,
          duration: 4,
          sourceFile: (file as any).path ?? file.name,
          name: file.name,
        }).catch(() => {});
      }
    }
    useProjectStore.getState().syncSnapshot(rpc);
  }, [rpc, pps]);

  // --- Context menu handler ---
  const handleContextMenu = useCallback((e: React.MouseEvent, clip: typeof clips[0]) => {
    e.preventDefault();
    e.stopPropagation();
    useUiStore.getState().selectClip(clip.clipId, clip.trackIndex);
    setContextMenu({ x: e.clientX, y: e.clientY, type: "clip", clip });
  }, []);

  const handleMarkerContextMenu = useCallback((e: React.MouseEvent, markerIndex: number) => {
    e.preventDefault();
    e.stopPropagation();
    setContextMenu({ x: e.clientX, y: e.clientY, type: "marker", markerIndex });
  }, []);

  const handleDeleteClip = useCallback(() => {
    if (!contextMenu?.clip) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      try {
        await rpc.call("project.removeClip", { clipId: c.clipId });
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      } catch (e) {
        console.error("Failed to delete clip:", e);
      }
    })();
  }, [contextMenu]);

  const handleDuplicateClip = useCallback(() => {
    if (!contextMenu?.clip) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      await rpc.call("project.duplicateClip", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const handleSplitClip = useCallback(() => {
    if (!contextMenu?.clip) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      await rpc.call("project.sliceClipAtPlayhead", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const pasteClipboard = useCallback(async () => {
    const { clipClipboard } = useUiStore.getState();
    if (clipClipboard.length === 0) return;
    const transport = useTransportStore.getState().transport;
    const playheadBeats = transport.currentTimeSeconds * (transport.bpm / 60);
    const minStart = Math.min(...clipClipboard.map(c => c.startBeat));
    await rpc.call("project.beginTransaction", { name: "paste clips" });
    for (const clip of clipClipboard) {
      const newStart = playheadBeats + (clip.startBeat - minStart);
      if (clip.isMidi) {
        await rpc.call("project.addMidiClip", { trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, name: clip.name }).catch(() => {});
      } else {
        await rpc.call("project.addAudioClip", { trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, sourceFile: clip.sourceFile, name: clip.name }).catch(() => {});
      }
    }
    await rpc.call("project.endTransaction");
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  }, [rpc]);

  // --- Keyboard shortcuts ---
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      const tag = target?.tagName;
      if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
      // Skip when focus is inside a focusable custom widget (NoteGrid,
      // AutomationPanel) that handles its own key events. Prevents double-fire
      // (e.g. Delete deleting both notes and timeline clips).
      if (target !== document.body && target?.closest?.("[tabindex]")) return;

      const { selectedClipIds } = useUiStore.getState();
      const isPlaying = useTransportStore.getState().transport.isPlaying;

      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          (async () => {
            try {
              await rpc.call("project.beginTransaction", { name: "delete clips" });
              for (const id of selectedClipIds) {
                await rpc.call("project.removeClip", { clipId: id });
              }
              await rpc.call("project.endTransaction");
              useUiStore.getState().clearSelection();
              await useProjectStore.getState().syncDirtyFlag(rpc);
              await useProjectStore.getState().syncSnapshot(rpc);
            } catch (e) {
              console.error("Failed to delete clips:", e);
            }
          })();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyD") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          (async () => {
            await rpc.call("project.beginTransaction", { name: "duplicate clips" });
            for (const id of selectedClipIds) {
              await rpc.call("project.duplicateClip", { clipId: id }).catch(() => {});
            }
            await rpc.call("project.endTransaction");
            await useProjectStore.getState().syncDirtyFlag(rpc);
            await useProjectStore.getState().syncSnapshot(rpc);
          })();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyC") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          const snap = useProjectStore.getState().snapshot;
          if (snap) {
            const copied = snap.clips.filter(c => selectedClipIds.has(c.clipId));
            useUiStore.getState().setClipboard(copied);
          }
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyX") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          const snap = useProjectStore.getState().snapshot;
          if (snap) {
            const copied = snap.clips.filter(c => selectedClipIds.has(c.clipId));
            useUiStore.getState().setClipboard(copied);
            (async () => {
              await rpc.call("project.beginTransaction", { name: "cut clips" });
              for (const id of selectedClipIds) {
                await rpc.call("project.removeClip", { clipId: id }).catch(() => {});
              }
              await rpc.call("project.endTransaction");
              useUiStore.getState().clearSelection();
              await useProjectStore.getState().syncDirtyFlag(rpc);
              await useProjectStore.getState().syncSnapshot(rpc);
            })();
          }
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyV") {
        e.preventDefault();
        const { clipClipboard } = useUiStore.getState();
        if (clipClipboard.length > 0) {
          pasteClipboard();
        }
      } else if (e.key === "Escape") {
        setContextMenu(null);
        setEmptyContextMenu(null);
      } else if (e.code === "KeyF" && e.shiftKey) {
        const snap = useProjectStore.getState().snapshot;
        const selIds = useUiStore.getState().selectedClipIds;
        const selectedClips = snap?.clips.filter((c) => selIds.has(c.clipId));
        if (selectedClips && selectedClips.length > 0) {
          const minStart = Math.min(...selectedClips.map((c) => c.startBeat));
          const maxEnd = Math.max(...selectedClips.map((c) => c.startBeat + c.durationBeats));
          const range = maxEnd - minStart;
          if (range > 0) {
            const cw = tracksRef.current?.clientWidth ?? 800;
            const newPps = (cw * 0.8) / range;
            setPps(Math.max(MIN_PPS, Math.min(MAX_PPS, newPps)));
            requestAnimationFrame(() => {
              if (tracksRef.current) {
                tracksRef.current.scrollLeft = minStart * pps - cw * 0.1;
              }
            });
          }
        }
        e.preventDefault();
      } else if (e.key === " ") {
        e.preventDefault();
        if (isPlaying)
          rpc.call("transport.stop").catch(() => {});
        else
          rpc.call("transport.play").catch(() => {});
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [pasteClipboard]);

  return (
    <div className="timeline-minimal">
      {/* Toolbar */}
      <div className="tl-toolbar">
        <button className="tl-tb-btn" onClick={zoomOut} title="Zoom Out">−</button>
        <span className="tl-tb-label">{pps} px/beat</span>
        <button className="tl-tb-btn" onClick={zoomIn} title="Zoom In">+</button>
        <button className="tl-tb-btn" onClick={zoomFit} title="Fit All">⟷</button>
      </div>

      {/* Body */}
      <div className="tl-body" onWheel={onWheel} onDragOver={(e) => e.preventDefault()} onDrop={handleDrop}>
        {/* Ruler */}
        <div className={`tl-ruler${isScrubbing ? " tl-ruler--scrubbing" : ""}`} ref={rulerRef} style={{ height: RULER_HEIGHT }} onMouseDown={handleRulerMouseDown} onContextMenu={(e) => {
          e.preventDefault();
          const el = tracksRef.current;
          if (!el) return;
          const rect = el.getBoundingClientRect();
          const beat = (e.clientX - rect.left + el.scrollLeft) / pps;
          const name = prompt("Marker name:", "Marker");
          if (name != null) {
            rpc.call("project.addMarker", { name, time: beat }).then(() => {
              useMarkerStore.getState().syncMarkers(rpc);
            }).catch(() => {});
          }
        }}>
          <div className="tl-ruler-inner" style={{ width: totalW, position: "relative" }}>
            {rulerMarkers.map((m) => (
              <div
                key={m.beat}
                className={`tl-ruler-mark ${m.isBar ? "tl-ruler-bar" : "tl-ruler-beat"}`}
                style={{ left: m.beat * pps }}
              >
                {m.isBar && <span className="tl-ruler-label">{Math.floor(m.beat / 4) + 1}</span>}
              </div>
            ))}
            {showLoop && (
              <div className="tl-loop-band" style={{ left: loopLX, width: loopRX - loopLX }} />
            )}
            {showLoop && (
              <>
                <div className="tl-loop-handle tl-loop-handle--start" style={{ left: loopLX }} onMouseDown={startLoopDrag("start")} />
                <div className="tl-loop-handle tl-loop-handle--end" style={{ left: loopRX }} onMouseDown={startLoopDrag("end")} />
              </>
            )}
            {markers.map((m) => (
              <div
                key={m.index}
                className="tl-marker-pin"
                style={{ left: m.time * pps }}
                title={m.name}
                onClick={(e) => {
                  e.stopPropagation();
                  const sec = m.time * 60 / transport.bpm;
                  rpc.call("transport.seekToSeconds", { seconds: sec }).catch(() => {});
                }}
                onContextMenu={(e) => handleMarkerContextMenu(e, m.index)}
                onDoubleClick={(e) => {
                  e.stopPropagation();
                  const newName = prompt("Marker name:", m.name);
                  if (newName != null) {
                    rpc.call("project.setMarkerName", { index: m.index, name: newName }).then(() => {
                      useMarkerStore.getState().syncMarkers(rpc);
                    }).catch(() => {});
                  }
                }}
              />
            ))}
          </div>
        </div>

        {/* Tracks area */}
        <div
          className="tl-tracks"
          ref={tracksRef}
          onScroll={onTracksScroll}
          style={dragCursor ? { cursor: dragCursor } : undefined}
        >
          <div className="tl-tracks-inner" style={{ width: totalW, height: totalH, position: "relative" }}
            onClick={() => {
              if (rubberBandJustCompleted.current) {
                rubberBandJustCompleted.current = false;
                return;
              }
              useUiStore.getState().clearSelection();
            }}
            onMouseDown={handleRubberBandStart}
            onContextMenu={(e) => {
              if ((e.target as HTMLElement).closest(".tl-clip")) return;
              e.preventDefault();
              const el = tracksRef.current;
              if (!el) return;
              const rect = el.getBoundingClientRect();
              const beat = (e.clientX - rect.left + el.scrollLeft) / pps;
              setEmptyContextMenu({ x: e.clientX, y: e.clientY, beat });
            }}>
            {tracks.map((track, idx) => {
              const trackClips = clipsByTrack.get(track.index) ?? [];
              const isTarget = dragState && idx === Math.min(Math.max(0, Math.floor(dragState.mouseY / TRACK_HEIGHT)), tracks.length - 1);
              return (
                <div
                  key={track.index}
                  className={`tl-track-row${isTarget ? " tl-track-row--target" : ""}`}
                  style={{ top: idx * TRACK_HEIGHT, height: TRACK_HEIGHT }}
                >
                    {trackClips.map((clip) => {
                    const isDragging = dragState != null && dragSelectedIdsRef.current.has(clip.clipId);
                    const isTrimming = trimState?.clipId === clip.clipId;
                    const isSelected = selectedClipIds.has(clip.clipId);
                    const dispLeft = isTrimming ? trimState.currentStartBeat * pps : clip.startBeat * pps;
                    const dispWidth = isTrimming ? Math.max(4, trimState.currentDuration * pps) : Math.max(4, clip.durationBeats * pps);
                    return (
                      <div
                        key={clip.clipId}
                        className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}${isSelected ? " tl-clip--selected" : ""}${clip.isGhost ? " tl-clip--ghost" : ""}`}
                        style={{ left: dispLeft, width: dispWidth, height: TRACK_HEIGHT - 8, top: 4, zIndex: isTrimming ? 3 : undefined, ...(clip.isMidi ? {} : { background: "transparent" }) }}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (e.ctrlKey || e.metaKey) {
                            useUiStore.getState().toggleClipSelection(clip.clipId);
                          } else if (e.shiftKey) {
                            const anchor = useUiStore.getState().lastSelectedClipId;
                            if (anchor != null) {
                              useUiStore.getState().selectRange(anchor, clip.clipId, clips);
                            } else {
                              useUiStore.getState().selectClip(clip.clipId, idx);
                            }
                          } else {
                            useUiStore.getState().selectClip(clip.clipId, idx);
                            useUiStore.getState().setActiveBottomTab(clip.isMidi ? "piano-roll" : "audio-editor");
                          }
                        }}
                        onDoubleClick={(e) => {
                          e.stopPropagation();
                          if (clip.isGhost && clip.ghostSourceId >= 0) {
                            const sourceClip = clips.find(c => c.clipId === clip.ghostSourceId);
                            if (sourceClip) {
                              useUiStore.getState().selectClip(sourceClip.clipId, sourceClip.trackIndex);
                              return;
                            }
                          }
                          useUiStore.getState().selectClip(clip.clipId, idx);
                          useUiStore.getState().setActiveBottomTab(clip.isMidi ? "piano-roll" : "audio-editor");
                        }}
                        onContextMenu={(e) => handleContextMenu(e, clip)}
                        onMouseDown={(e) => { if (!isTrimming) handleClipMouseDown(e, clip.clipId, idx, clip.startBeat); }}
                      >
                        {!clip.isMidi && (
                          <WaveformCanvas clip={clip} width={Math.max(4, dispWidth)} height={TRACK_HEIGHT - 8} />
                        )}
                        {(clip.fadeIn > 0 || clip.fadeOut > 0 || (fadeDrag?.clipId === clip.clipId)) && (
                          <svg viewBox="0 0 100 48" preserveAspectRatio="none" style={{ position: "absolute", top: 0, left: 0, width: "100%", height: "100%", pointerEvents: "none" }}>
                            <path
                              d={`M0,48 L${((fadeDrag?.clipId === clip.clipId && fadeDrag.side === "in" ? fadeDrag.initialValue : clip.fadeIn) / clip.durationBeats) * 100},0 L${100 - ((fadeDrag?.clipId === clip.clipId && fadeDrag.side === "out" ? fadeDrag.initialValue : clip.fadeOut) / clip.durationBeats) * 100},0 L100,48`}
                              fill="rgba(255,255,255,0.1)"
                              stroke="rgba(255,255,255,0.3)"
                              strokeWidth="1"
                            />
                          </svg>
                        )}
                        <span className="tl-clip-name" style={{ position: "absolute", bottom: 2, left: 4 }}>{clip.name ?? `Clip ${clip.clipId}`}</span>
                        <div className="fade-handle fade-handle-in" onMouseDown={(e) => handleFadeStart(e, clip, "in")} onClick={(e) => e.stopPropagation()} />
                        <div className="fade-handle fade-handle-out" onMouseDown={(e) => handleFadeStart(e, clip, "out")} onClick={(e) => e.stopPropagation()} />
                        <div className="clip-trim clip-trim-left" onMouseDown={(e) => handleTrimStart(e, clip, "left")} onClick={(e) => e.stopPropagation()} />
                        <div className="clip-trim clip-trim-right" onMouseDown={(e) => handleTrimStart(e, clip, "right")} onClick={(e) => e.stopPropagation()} />
                        {clip.looping && (
                          <div
                            className="tl-loop-paint-handle"
                            title="Drag to paint repetitions"
                            onMouseDown={(e) => {
                              e.stopPropagation();
                              e.preventDefault();
                              handleClipMouseDown(e, clip.clipId, idx, clip.startBeat, true);
                            }}
                            onClick={(e) => e.stopPropagation()}
                          />
                        )}
                      </div>
                    );
                  })}
                </div>
              );
            })}

            {/* Loop band on tracks */}
            {showLoop && (
              <div className="tl-loop-band-tracks" style={{ left: loopLX, width: loopRX - loopLX, height: totalH }} />
            )}

            {/* Playhead */}
            <div className="tl-playhead" style={{ left: playheadX }} />

            {/* Paint tile previews */}
            {paintTiles.map((tile, i) => (
              <div key={`paint-${i}`} className="tl-paint-tile tl-paint-tile--pending" style={{ left: tile.left, width: tile.width, top: tile.top, height: TRACK_HEIGHT - 8 }} />
            ))}
            {dragState?.paintRepeat && dragState.paintedClipIds.length > 0 && (
              <span className="tl-paint-badge" style={{ left: dragState.paintOriginBeat * pps + dragState.paintSpacing * dragState.paintedClipIds.length * pps }}>+{dragState.paintedClipIds.length}</span>
            )}

            {/* Drag preview */}
            {dragPreviewStyle && dragPreviewClip && (
              <div
                className={`tl-clip tl-ghost ${dragPreviewClip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}`}
                style={{ ...dragPreviewStyle, ...(dragPreviewClip.isMidi ? {} : { background: "transparent" }) }}
              >
                {!dragPreviewClip.isMidi && (
                  <WaveformCanvas clip={dragPreviewClip} width={Math.max(4, dragPreviewClip.durationBeats * pps)} height={TRACK_HEIGHT - 8} />
                )}
                <span className="tl-clip-name" style={{ position: "absolute", bottom: 2, left: 4 }}>{dragPreviewClip.name ?? `Clip ${dragPreviewClip.clipId}`}</span>
              </div>
            )}

            {/* Rubber band selection */}
            {rubberBand && (
              <div
                className="tl-rubber-band"
                style={{
                  left: Math.min(rubberBand.x1, rubberBand.x2),
                  top: Math.min(rubberBand.y1, rubberBand.y2),
                  width: Math.abs(rubberBand.x2 - rubberBand.x1),
                  height: Math.abs(rubberBand.y2 - rubberBand.y1),
                }}
              />
            )}
          </div>
        </div>
      </div>

      {/* Context menu */}
      {contextMenu && (
        <div
          className="clip-context-menu"
          onClick={(e) => e.stopPropagation()}
          style={{ left: contextMenu.x, top: contextMenu.y }}
        >
          {contextMenu.type === "clip" && contextMenu.clip && (
            <>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                handleDeleteClip();
              }}>
                Delete
              </button>
              <button onMouseDown={(e) => { e.stopPropagation(); handleDuplicateClip(); }}>
                Duplicate
              </button>
              <div className="ctx-separator" />
              <button
                className={contextMenu.clip.looping ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  const clipId = contextMenu.clip!.clipId;
                  const newLooping = !contextMenu.clip!.looping;
                  rpc.call("project.setClipLooping", { clipId, looping: newLooping }).then(() => {
                    useProjectStore.getState().syncSnapshot(rpc);
                  });
                  setContextMenu(null);
                }}
              >
                {contextMenu.clip.looping ? "✓ " : ""}Looped
              </button>
              <button
                className={contextMenu.clip.muted ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  const clipId = contextMenu.clip!.clipId;
                  const newMuted = !contextMenu.clip!.muted;
                  rpc.call("project.setClipMuted", { clipId, muted: newMuted }).then(() => {
                    useProjectStore.getState().syncSnapshot(rpc);
                  });
                  setContextMenu(null);
                }}
              >
                {contextMenu.clip.muted ? "✓ " : ""}Muted
              </button>
              <div className="ctx-separator" />
              {contextMenu.clip.isGhost && contextMenu.clip.ghostSourceId >= 0 && (
                <button onMouseDown={(e) => {
                  e.stopPropagation();
                  setContextMenu(null);
                  const sourceClip = clips.find(c => c.clipId === contextMenu.clip!.ghostSourceId);
                  if (sourceClip) {
                    useUiStore.getState().selectClip(sourceClip.clipId, sourceClip.trackIndex);
                  }
                }}>
                  Select Original
                </button>
              )}
              <button onMouseDown={(e) => { e.stopPropagation(); handleSplitClip(); }}>
                Split
              </button>
              <button onMouseDown={(e) => { e.stopPropagation(); useUiStore.getState().setClipboard([contextMenu.clip!]); setContextMenu(null); }}>
                Copy
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                useUiStore.getState().setClipboard([contextMenu.clip!]);
                setContextMenu(null);
                rpc.call("project.beginTransaction", { name: "cut clip" }).then(() =>
                  rpc.call("project.removeClip", { clipId: contextMenu.clip!.clipId })
                ).then(() => rpc.call("project.endTransaction")).then(() => {
                  useProjectStore.getState().syncDirtyFlag(rpc);
                  useProjectStore.getState().syncSnapshot(rpc);
                }).catch(() => {});
              }}>
                Cut
              </button>
              <div className="ctx-separator" />
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.sliceClipAtPlayhead", { clipId });
                setContextMenu(null);
              }}>
                Slice at Playhead
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.sliceClipAtTransients", { clipId });
                setContextMenu(null);
              }}>
                Slice at Transients
              </button>
              <div className="ctx-separator" />
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.copyAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
                setContextMenu(null);
              }}>
                Copy Region
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.cutAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
                setContextMenu(null);
              }}>
                Cut Region
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.pasteAudioClipRegion", { clipId, pasteTime: transport.currentTimeSeconds });
                setContextMenu(null);
              }}>
                Paste Region
              </button>
            </>
          )}
          {contextMenu.type === "marker" && (
            <>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const marker = markers.find(m => m.index === contextMenu.markerIndex);
                const name = prompt("Marker name:", marker?.name ?? "");
                if (name != null && contextMenu.markerIndex != null) {
                  rpc.call("project.setMarkerName", { index: contextMenu.markerIndex, name }).then(() => {
                    useMarkerStore.getState().syncMarkers(rpc);
                  }).catch(() => {});
                }
                setContextMenu(null);
              }}>
                Rename Marker
              </button>
              <button className="ctx-danger" onMouseDown={(e) => {
                e.stopPropagation();
                if (contextMenu.markerIndex != null) {
                  rpc.call("project.removeMarker", { index: contextMenu.markerIndex }).then(() => {
                    useMarkerStore.getState().syncMarkers(rpc);
                  }).catch(() => {});
                }
                setContextMenu(null);
              }}>
                Delete Marker
              </button>
            </>
          )}
        </div>
      )}

      {/* Empty-area context menu */}
      {emptyContextMenu && (
        <div className="clip-context-menu" style={{ left: emptyContextMenu.x, top: emptyContextMenu.y }}
          onMouseDown={(e) => e.stopPropagation()}>
          <button onMouseDown={(e) => { e.stopPropagation(); rpc.call("project.addTrack").catch(() => {}); setEmptyContextMenu(null); }}>
            Add Track
          </button>
          {useUiStore.getState().clipClipboard.length > 0 && (
            <button onMouseDown={(e) => {
              e.stopPropagation();
              setEmptyContextMenu(null);
              pasteClipboard();
            }}>
              Paste
            </button>
          )}
          <button onMouseDown={(e) => {
            e.stopPropagation();
            const bpm = prompt("BPM:", "120");
            if (bpm) rpc.call("project.setTempo", { bpm: parseFloat(bpm) || 120 }).catch(() => {});
            setEmptyContextMenu(null);
          }}>
            Set Global BPM...
          </button>
          <button onMouseDown={(e) => {
            e.stopPropagation();
            rpc.call("project.addMidiClip", {
              trackIndex: 0,
              start: emptyContextMenu.beat,
              duration: 4,
              name: "New MIDI Clip",
            }).catch(() => {});
            setEmptyContextMenu(null);
          }}>
            Add MIDI Clip
          </button>
        </div>
      )}
    </div>
  );
}
