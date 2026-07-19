import { useState, useMemo, useRef, useCallback, useEffect } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { WaveformCanvas } from "./WaveformCanvas";
import "./TimelineMinimal.css";

const DEFAULT_PPS = 40;
const MIN_PPS = 10;
const MAX_PPS = 200;
const TRACK_HEIGHT = 56;
const RULER_HEIGHT = 24;

interface DragState {
  clipId: number;
  startTrackIndex: number;
  startBeat: number;
  offsetX: number;
  offsetY: number;
  mouseX: number;
  mouseY: number;
  isAltDuplicate?: boolean;
  altDuplicated?: boolean;
}

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
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];

  const rulerRef = useRef<HTMLDivElement>(null);
  const tracksRef = useRef<HTMLDivElement>(null);

  // --- Clip drag state ---
  const [dragState, setDragState] = useState<DragState | null>(null);
  const dragRef = useRef<DragState | null>(null);
  const updateDrag = useCallback((next: DragState | null) => {
    dragRef.current = next;
    setDragState(next);
  }, []);

  // --- Trim state ---
  const [trimState, setTrimState] = useState<TrimState | null>(null);
  const trimRef = useRef<TrimState | null>(null);
  const updateTrim = useCallback((next: TrimState | null) => {
    trimRef.current = next;
    setTrimState(next);
  }, []);

  // --- Loop drag state ---
  const [loopDrag, setLoopDrag] = useState<"start" | "end" | null>(null);
  const [dragBeat, setDragBeat] = useState(0);
  const dragBeatRef = useRef(0);

  // --- Rubber band state ---
  const [rubberBand, setRubberBand] = useState<{ x1: number; y1: number; x2: number; y2: number } | null>(null);
  const rubberBandRef = useRef(rubberBand);
  rubberBandRef.current = rubberBand;

  // --- Context menu ---
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; clip: typeof clips[0] } | null>(null);
  const [emptyContextMenu, setEmptyContextMenu] = useState<{ x: number; y: number; beat: number } | null>(null);

  useEffect(() => {
    const close = () => { setContextMenu(null); setEmptyContextMenu(null); };
    window.addEventListener("click", close);
    return () => window.removeEventListener("click", close);
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
      setLoopDrag(null);
      const finalBeat = dragBeatRef.current;
      const method = which === "start" ? "project.setLoopStart" : "project.setLoopEnd";
      rpc.call(method, which === "start" ? { beat: finalBeat } : { beat: finalBeat }).catch(() => {});
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps, transport.loopStart, transport.loopEnd, dragBeat]);

  // --- Rubber band handler ---
  const handleRubberBandStart = useCallback((e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest(".tl-clip")) return;
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left + el.scrollLeft;
    const y = e.clientY - rect.top + el.scrollTop;
    setRubberBand({ x1: x, y1: y, x2: x, y2: y });

    const onMove = (ev: globalThis.MouseEvent) => {
      const r = el.getBoundingClientRect();
      setRubberBand(prev => prev ? {
        ...prev,
        x2: ev.clientX - r.left + el.scrollLeft,
        y2: ev.clientY - r.top + el.scrollTop,
      } : null);
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const rb = rubberBandRef.current;
      if (rb) {
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
        if (selected.size > 0) {
          useUiStore.setState({ selectedClipIds: selected });
        }
      }
      setRubberBand(null);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [clips, pps]);

  // --- Clip drag handlers (Phase 3) ---
  const dragSelectedIdsRef = useRef<Set<number>>(new Set());

  const handleClipMouseDown = useCallback(
    (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number) => {
      e.preventDefault();
      const el = e.currentTarget as HTMLElement;
      const r = el.getBoundingClientRect();
      const selected = useUiStore.getState().selectedClipIds;
      dragSelectedIdsRef.current = selected.has(clipId) ? new Set(selected) : new Set([clipId]);
      updateDrag({ clipId, startTrackIndex: trackIndex, startBeat, offsetX: e.clientX - r.left, offsetY: e.clientY - r.top, mouseX: e.clientX, mouseY: e.clientY, isAltDuplicate: e.altKey ? true : undefined });
    },
    [updateDrag]
  );

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
        const mouseBeat = (ev.clientX - rect.left + scroll) / pps;

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
        updateTrim(null);

        if (d.side === "left") {
          if (Math.abs(d.currentStartBeat - d.initialStartBeat) > 0.01) {
            (async () => {
              try {
                await rpc.call("project.beginTransaction", { name: "trim clip" });
                await rpc.call("project.setClipStart", { clipId: d.clipId, start: d.currentStartBeat });
                await rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration });
                await rpc.call("project.endTransaction");
              } catch (e) { console.error("trim failed", e); }
            })();
          }
        } else {
          if (Math.abs(d.currentDuration - d.initialDuration) > 0.01) {
            rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration }).catch(() => {});
          }
        }
      };

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [pps, updateTrim]
  );

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    const d = dragRef.current;
    if (!d) return;
    if (d.isAltDuplicate && !d.altDuplicated) {
      const { snapshot } = useProjectStore.getState();
      if (!snapshot) return;
      const ids = dragSelectedIdsRef.current;
      const newIds = new Set<number>();
      (async () => {
        await rpc.call("project.beginTransaction", { name: "alt-duplicate clips" });
        for (const id of ids) {
          const r = await rpc.call("project.duplicateClip", { clipId: id }).catch(() => null);
          if (r && typeof r === "object" && "clipId" in r) newIds.add((r as { clipId: number }).clipId);
        }
        await rpc.call("project.endTransaction");
        if (newIds.size > 0) {
          useUiStore.setState({ selectedClipIds: newIds });
          dragSelectedIdsRef.current = newIds;
          const first = [...newIds][0];
          dragRef.current = { ...d, clipId: first, altDuplicated: true };
          setDragState(dragRef.current);
        }
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      })();
      return;
    }
    updateDrag({ ...d, mouseX: e.clientX, mouseY: e.clientY });
  }, [updateDrag]);

  const handleMouseUp = useCallback(() => {
    const d = dragRef.current;
    if (!d) return;
    updateDrag(null);
    const el = tracksRef.current;
    if (!el) return;
    const cr = el.getBoundingClientRect();
    const relX = d.mouseX - cr.left;
    const relY = d.mouseY - cr.top;
    const newStart = Math.max(0, (relX - d.offsetX) / pps);
    const newTrackIndex = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), tracks.length - 1);
    if (newTrackIndex !== d.startTrackIndex || Math.abs(newStart - d.startBeat) > 0.01) {
      const deltaStart = newStart - d.startBeat;
      const deltaTrack = newTrackIndex - d.startTrackIndex;
      const ids = dragSelectedIdsRef.current;
      (async () => {
        await rpc.call("project.beginTransaction", { name: "move clips" });
        for (const id of ids) {
          const clip = clips.find(c => c.clipId === id);
          if (!clip) continue;
          const clipNewStart = Math.max(0, clip.startBeat + deltaStart);
          const clipNewTrack = Math.min(Math.max(0, clip.trackIndex + deltaTrack), tracks.length - 1);
          await rpc.call("project.moveClip", { clipId: id, newTrackIndex: clipNewTrack, newStart: clipNewStart }).catch(() => {});
        }
        await rpc.call("project.endTransaction");
      })();
    }
  }, [pps, tracks.length, clips, updateDrag]);

  const handleMouseLeave = useCallback(() => updateDrag(null), [updateDrag]);

  // --- Context menu handler ---
  const handleContextMenu = useCallback((e: React.MouseEvent, clip: typeof clips[0]) => {
    e.preventDefault();
    e.stopPropagation();
    useUiStore.getState().selectClip(clip.clipId, clip.trackIndex);
    setContextMenu({ x: e.clientX, y: e.clientY, clip });
  }, []);

  const handleDeleteClip = useCallback(() => {
    if (!contextMenu) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      await rpc.call("project.removeClip", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const handleDuplicateClip = useCallback(() => {
    if (!contextMenu) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      await rpc.call("project.duplicateClip", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const handleSplitClip = useCallback(() => {
    if (!contextMenu) return;
    const c = contextMenu.clip;
    setContextMenu(null);
    (async () => {
      await rpc.call("project.sliceClipAtPlayhead", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  // --- Keyboard shortcuts ---
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const tag = (e.target as HTMLElement)?.tagName;
      if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;

      const { selectedClipIds } = useUiStore.getState();
      const isPlaying = useTransportStore.getState().transport.isPlaying;

      if (e.key === "Delete" || e.key === "Backspace") {
        if (selectedClipIds.size > 0) {
          e.preventDefault();
          (async () => {
            await rpc.call("project.beginTransaction", { name: "delete clips" });
            for (const id of selectedClipIds) {
              await rpc.call("project.removeClip", { clipId: id }).catch(() => {});
            }
            await rpc.call("project.endTransaction");
            useUiStore.getState().clearSelection();
            await useProjectStore.getState().syncDirtyFlag(rpc);
            await useProjectStore.getState().syncSnapshot(rpc);
          })();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === "d") {
        if (selectedClipIds.size > 0) {
          e.preventDefault();
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
      } else if ((e.ctrlKey || e.metaKey) && e.key === "c") {
        if (selectedClipIds.size > 0) {
          e.preventDefault();
          const snap = useProjectStore.getState().snapshot;
          if (snap) {
            const copied = snap.clips.filter(c => selectedClipIds.has(c.clipId));
            useUiStore.getState().setClipboard(copied);
          }
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === "x") {
        if (selectedClipIds.size > 0) {
          e.preventDefault();
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
      } else if ((e.ctrlKey || e.metaKey) && e.key === "v") {
        const { clipClipboard } = useUiStore.getState();
        if (clipClipboard.length > 0) {
          e.preventDefault();
          const t = useTransportStore.getState().transport;
          const phBeats = t.currentTimeSeconds * (t.bpm / 60);
          const minStart = Math.min(...clipClipboard.map(c => c.startBeat));
          (async () => {
            await rpc.call("project.beginTransaction", { name: "paste clips" });
            for (const clip of clipClipboard) {
              const newStart = phBeats + (clip.startBeat - minStart);
              if (clip.isMidi) {
                await rpc.call("project.addMidiClip", {
                  trackIndex: clip.trackIndex,
                  start: newStart,
                  duration: clip.durationBeats,
                  name: clip.name,
                }).catch(() => {});
              } else {
                await rpc.call("project.addAudioClip", {
                  trackIndex: clip.trackIndex,
                  start: newStart,
                  duration: clip.durationBeats,
                  sourceFile: clip.sourceFile,
                  name: clip.name,
                }).catch(() => {});
              }
            }
            await rpc.call("project.endTransaction");
            await useProjectStore.getState().syncDirtyFlag(rpc);
            await useProjectStore.getState().syncSnapshot(rpc);
          })();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === "z") {
        e.preventDefault();
        (async () => {
          if (e.shiftKey)
            await rpc.call("project.redo").catch(() => {});
          else
            await rpc.call("project.undo").catch(() => {});
          await useProjectStore.getState().syncDirtyFlag(rpc);
          await useProjectStore.getState().syncSnapshot(rpc);
        })();
      } else if (e.key === "Escape") {
        setContextMenu(null);
        setEmptyContextMenu(null);
      } else if (e.key === " " && e.target === document.body) {
        e.preventDefault();
        if (isPlaying)
          rpc.call("transport.stop").catch(() => {});
        else
          rpc.call("transport.play").catch(() => {});
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);

  // --- Ghost clip for drag ---
  let ghostStyle: React.CSSProperties | undefined;
  let ghostClip: (typeof clips)[0] | undefined;
  if (dragState) {
    const el = tracksRef.current;
    if (el) {
      const cr = el.getBoundingClientRect();
      const relX = dragState.mouseX - cr.left;
      const relY = dragState.mouseY - cr.top;
      const gs = Math.max(0, (relX - dragState.offsetX) / pps);
      const gi = Math.min(Math.max(0, Math.floor(relY / TRACK_HEIGHT)), tracks.length - 1);
      const orig = clips.find((c) => c.clipId === dragState.clipId);
      if (orig) {
        ghostClip = orig;
        ghostStyle = { left: gs * pps, width: Math.max(4, orig.durationBeats * pps), height: TRACK_HEIGHT - 8, top: gi * TRACK_HEIGHT + 4 };
      }
    }
  }

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
      <div className="tl-body" onWheel={onWheel}>
        {/* Ruler */}
        <div className={`tl-ruler${isScrubbing ? " tl-ruler--scrubbing" : ""}`} ref={rulerRef} style={{ height: RULER_HEIGHT }} onMouseDown={handleRulerMouseDown}>
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
          </div>
        </div>

        {/* Tracks area */}
        <div
          className="tl-tracks"
          ref={tracksRef}
          onScroll={onTracksScroll}
          onMouseMove={handleMouseMove}
          onMouseUp={handleMouseUp}
          onMouseLeave={handleMouseLeave}
        >
          <div className="tl-tracks-inner" style={{ width: totalW, height: totalH, position: "relative" }}
            onClick={() => useUiStore.getState().clearSelection()}
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
                    const isDragging = dragState?.clipId === clip.clipId;
                    const isTrimming = trimState?.clipId === clip.clipId;
                    const isSelected = selectedClipIds.has(clip.clipId);
                    const dispLeft = isTrimming ? trimState.currentStartBeat * pps : clip.startBeat * pps;
                    const dispWidth = isTrimming ? Math.max(4, trimState.currentDuration * pps) : Math.max(4, clip.durationBeats * pps);
                    return (
                      <div
                        key={clip.clipId}
                        className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}${isSelected ? " tl-clip--selected" : ""}`}
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
                          }
                        }}
                        onContextMenu={(e) => handleContextMenu(e, clip)}
                        onMouseDown={(e) => { if (!isTrimming) handleClipMouseDown(e, clip.clipId, idx, clip.startBeat); }}
                      >
                        {!clip.isMidi && (
                          <WaveformCanvas clip={clip} width={Math.max(4, dispWidth)} height={TRACK_HEIGHT - 8} />
                        )}
                        <span className="tl-clip-name" style={{ position: "absolute", bottom: 2, left: 4 }}>{clip.name ?? `Clip ${clip.clipId}`}</span>
                        <div className="clip-trim clip-trim-left" onMouseDown={(e) => handleTrimStart(e, clip, "left")} />
                        <div className="clip-trim clip-trim-right" onMouseDown={(e) => handleTrimStart(e, clip, "right")} />
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

            {/* Ghost clip */}
            {ghostStyle && ghostClip && (
              <div
                className={`tl-clip tl-ghost ${ghostClip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}`}
                style={{ ...ghostStyle, ...(ghostClip.isMidi ? {} : { background: "transparent" }) }}
              >
                {!ghostClip.isMidi && (
                  <WaveformCanvas clip={ghostClip} width={Math.max(4, ghostClip.durationBeats * pps)} height={TRACK_HEIGHT - 8} />
                )}
                <span className="tl-clip-name" style={{ position: "absolute", bottom: 2, left: 4 }}>{ghostClip.name ?? `Clip ${ghostClip.clipId}`}</span>
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
          <button onClick={handleDeleteClip}>
            Delete
          </button>
          <button onClick={handleDuplicateClip}>
            Duplicate
          </button>
          <button onClick={handleSplitClip}>
            Split
          </button>
          <button onClick={() => { useUiStore.getState().setClipboard([contextMenu.clip]); setContextMenu(null); }}>
            Copy
          </button>
          <button onClick={() => {
            useUiStore.getState().setClipboard([contextMenu.clip]);
            setContextMenu(null);
            rpc.call("project.beginTransaction", { name: "cut clip" }).then(() =>
              rpc.call("project.removeClip", { clipId: contextMenu.clip.clipId })
            ).then(() => rpc.call("project.endTransaction")).then(() => {
              useProjectStore.getState().syncDirtyFlag(rpc);
              useProjectStore.getState().syncSnapshot(rpc);
            }).catch(() => {});
          }}>
            Cut
          </button>
        </div>
      )}

      {/* Empty-area context menu */}
      {emptyContextMenu && (
        <div className="clip-context-menu" style={{ left: emptyContextMenu.x, top: emptyContextMenu.y }}
          onClick={(e) => e.stopPropagation()}>
          <button onClick={() => { rpc.call("project.addTrack").catch(() => {}); setEmptyContextMenu(null); }}>
            Add Track
          </button>
          {useUiStore.getState().clipClipboard.length > 0 && (
            <button onClick={() => {
              setEmptyContextMenu(null);
              const t = useTransportStore.getState().transport;
              const phBeats = t.currentTimeSeconds * (t.bpm / 60);
              const cb = useUiStore.getState().clipClipboard;
              const minStart = Math.min(...cb.map(c => c.startBeat));
              (async () => {
                await rpc.call("project.beginTransaction", { name: "paste clips" });
                for (const clip of cb) {
                  const newStart = phBeats + (clip.startBeat - minStart);
                  if (clip.isMidi) {
                    await rpc.call("project.addMidiClip", {
                      trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, name: clip.name,
                    }).catch(() => {});
                  } else {
                    await rpc.call("project.addAudioClip", {
                      trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, sourceFile: clip.sourceFile, name: clip.name,
                    }).catch(() => {});
                  }
                }
                await rpc.call("project.endTransaction");
                await useProjectStore.getState().syncDirtyFlag(rpc);
                await useProjectStore.getState().syncSnapshot(rpc);
              })();
            }}>
              Paste
            </button>
          )}
          <button onClick={() => {
            const bpm = prompt("BPM:", "120");
            if (bpm) rpc.call("project.setTempo", { bpm: parseFloat(bpm) || 120 }).catch(() => {});
            setEmptyContextMenu(null);
          }}>
            Set Global BPM...
          </button>
          <button onClick={() => {
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
