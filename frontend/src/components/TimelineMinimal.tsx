import { useState, useMemo, useRef, useCallback, useEffect } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useMarkerStore } from "../store/markerStore";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { WaveformCanvas } from "./WaveformCanvas";
import { snapToGrid } from "./snapUtils";
import { useTimelineDrag } from "../hooks/useTimelineDrag";
import { useTimelineTrim } from "../hooks/useTimelineTrim";
import { useTimelineFade } from "../hooks/useTimelineFade";
import { useTimelineLoopDrag } from "../hooks/useTimelineLoopDrag";
import { useTimelineRubberBand } from "../hooks/useTimelineRubberBand";
import { useTimelineZoom, MIN_PPS, MAX_PPS } from "../hooks/useTimelineZoom";
import { TimelineContextMenu } from "./TimelineContextMenu";
import "./TimelineMinimal.css";

const TRACK_HEIGHT = 56;
const RULER_HEIGHT = 24;

export default function TimelineMinimal() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const transport = useTransportStore((s) => s.transport);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const markers = useMarkerStore((s) => s.markers);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];

  const rulerRef = useRef<HTMLDivElement>(null);
  const tracksRef = useRef<HTMLDivElement>(null);

  const maxEnd = clips.reduce((max, c) => Math.max(max, c.startBeat + c.durationBeats), 4);

  // --- Zoom (extracted hook) ---
  const { pps, setPps, zoomIn, zoomOut, zoomFit, onWheel } = useTimelineZoom({ maxEnd, tracksRef });

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

  // --- Trim (extracted hook) ---
  const { handleTrimStart, trimState } = useTimelineTrim({
    clips,
    pps,
    rpc,
    tracksRef,
  });

  // --- Fade (extracted hook) ---
  const { handleFadeStart, fadeDrag } = useTimelineFade({
    clips,
    pps,
    rpc,
    tracksRef,
  });

  // --- Loop drag (extracted hook) ---
  const { startLoopDrag, dispLoopStart, dispLoopEnd } = useTimelineLoopDrag({
    pps,
    transport,
    rpc,
    tracksRef,
  });

  // --- Rubber band (extracted hook) ---
  const { handleRubberBandStart, rubberBand, rubberBandJustCompleted } = useTimelineRubberBand({
    clips,
    pps,
    TRACK_HEIGHT,
    selectedClipIds,
    tracksRef,
  });

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
  const loopLX = Math.max(0, dispLoopStart) * pps;
  const loopRX = Math.max(loopLX / pps + 0.25, dispLoopEnd) * pps;

  // --- Scroll sync ---
  const onTracksScroll = useCallback(() => {
    if (rulerRef.current && tracksRef.current) {
      rulerRef.current.scrollLeft = tracksRef.current.scrollLeft;
    }
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

  const handleCloseContextMenu = useCallback(() => {
    setContextMenu(null);
    setEmptyContextMenu(null);
  }, []);

  const handleDeleteClip = useCallback(() => {
    if (!contextMenu?.clip) return;
    const c = contextMenu.clip;
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
    (async () => {
      await rpc.call("project.duplicateClip", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const handleSplitClip = useCallback(() => {
    if (!contextMenu?.clip) return;
    const c = contextMenu.clip;
    (async () => {
      await rpc.call("project.sliceClipAtPlayhead", { clipId: c.clipId }).catch(() => {});
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }, [contextMenu]);

  const pasteClipboard = useCallback(async () => {
    const { clipClipboard } = useUiStore.getState();
    if (clipClipboard.length === 0) return;
    const tr = useTransportStore.getState().transport;
    const playheadBeats = tr.currentTimeSeconds * (tr.bpm / 60);
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
  }, []);

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

      <TimelineContextMenu
        contextMenu={contextMenu}
        emptyContextMenu={emptyContextMenu}
        clips={clips}
        markers={markers}
        selectedClipIds={selectedClipIds}
        transport={transport}
        onClose={handleCloseContextMenu}
        onDeleteClip={handleDeleteClip}
        onDuplicateClip={handleDuplicateClip}
        onSplitClip={handleSplitClip}
      />
    </div>
  );
}
