import { useMemo, useRef, useCallback, useEffect, useState } from "react";
import { useProjectStore } from "../../store/projectStore";
import { useTransportStore } from "../../store/transportStore";
import { useTransportExtrasStore } from "../../store/transportExtrasStore";
import { useMarkerStore } from "../../store/markerStore";
import { rpc } from "../../rpc";
import { useUiStore } from "../../store/uiStore";
import { WaveformCanvas } from "../WaveformCanvas";
import { MidiThumbnailCanvas } from "../MidiThumbnailCanvas";
import { useTimelineDrag } from "../../hooks/useTimelineDrag";
import { useTimelineTrim } from "../../hooks/useTimelineTrim";
import { useTimelineFade } from "../../hooks/useTimelineFade";
import { useTimelineLoopDrag } from "../../hooks/useTimelineLoopDrag";
import { useTimelineRubberBand } from "../../hooks/useTimelineRubberBand";
import { useTimelineZoom } from "../../hooks/useTimelineZoom";
import { useTimelineRuler } from "./useTimelineRuler";
import { useTimelineDrop } from "./useTimelineDrop";
import { useTimelineKeyboard } from "./useTimelineKeyboard";
import { useTimelineClipOps } from "./useTimelineClipOps";
import { TimelineContextMenu } from "../TimelineContextMenu";
import { RULER_HEIGHT, TOOLBAR_HEIGHT } from "../../utils/timelineConstants";
import { buildRowLayout, rowAtY } from "../../utils/rowLayout";
import { getVisibleTracks } from "../../utils/timelineUtils";
import { colorStr } from "../../theme";
import { AddTrackMenu } from "../AddTrackMenu";
import PopUpBrowser from "../PopUpBrowser";
import { ArrangerLane } from "../ArrangerLane";
import "../TimelineMinimal.css";

export default function TimelineMinimal() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const transport = useTransportStore((s) => s.transport);
  const followPlayhead = useTransportExtrasStore((s) => s.followPlayhead);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const pendingTempIds = useProjectStore((s) => s.pendingTempIds);
  const markers = useMarkerStore((s) => s.markers);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];

  // Shared with TrackHeaders so header rows and lanes index the same tracks.
  const visibleTracks = useMemo(() => getVisibleTracks(tracks), [tracks]);
  // Single source of truth for row geometry (consumes each track's persisted
  // height). The lanes and the header column both lay out from this.
  const layout = useMemo(
    () => buildRowLayout(visibleTracks.map((t) => t.height)),
    [visibleTracks]
  );
  // Track color (formatted to CSS hex) by track index, used to tint each
  // clip with its owning track's color. Built from ALL tracks (not just
  // visible) since clips reference absolute track indices.
  const trackColorByIndex = useMemo(() => {
    const m = new Map<number, string>();
    for (const t of tracks) m.set(t.index, colorStr(t.color));
    return m;
  }, [tracks]);

  const rulerRef = useRef<HTMLDivElement>(null);
  const arrangerLaneRef = useRef<HTMLDivElement>(null);
  const tracksRef = useRef<HTMLDivElement>(null);
  const bodyRef = useRef<HTMLDivElement>(null);
  const engagementRef = useRef<"none" | "clip" | "rubber" | "zoom">("none");
  const [scrollLeft, setScrollLeft] = useState(0);

  const maxEnd = clips.reduce((max, c) => Math.max(max, c.startBeat + c.durationBeats), 4);

  // --- Zoom (extracted hook) ---
  const { pps, setPps, zoomIn, zoomOut, zoomFit, zoomToRange } = useTimelineZoom({ maxEnd, bodyRef, tracksRef, rulerRef });

  // --- Clip drag (extracted hook) ---
  const {
    dragState,
    handleClipMouseDown,
    dragSelectedIdsRef,
    dragCursor,
    dragPreviewStyle,
    dragPreviewClip,
    dragPreviewHeight,
    paintTiles,
    paintCount,
  } = useTimelineDrag({
    clips,
    pps,
    layout,
    tracksRef,
    trackCount: visibleTracks.length,
    rpc,
    engagementRef,
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
    layout,
    selectedClipIds,
    tracksRef,
    engagementRef,
    setPps,
  });

  useEffect(() => {
    const close = (e: MouseEvent) => {
      // Don't close if clicking inside context menu
      const target = e.target as HTMLElement;
      if (target.closest('.clip-context-menu')) {
        return;
      }
      setContextMenu(null);
      setEmptyContextMenu(null);
      setBelowMenu(null);
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
  const totalH = layout.total;

  // --- Playhead ---
  const playheadBeats = transport.currentTimeSeconds * (transport.bpm / 60);
  const playheadX = playheadBeats * pps;

  // --- Playhead auto-follow ---
  // While enabled and playing, keep the playhead inside the visible scroll
  // range (with a small right margin); re-center it ~20% from the left edge.
  // Setting scrollLeft fires onTracksScroll, which syncs ruler/arranger lane.
  useEffect(() => {
    if (!followPlayhead || !transport.isPlaying) return;
    const el = tracksRef.current;
    if (!el || el.clientWidth <= 0) return;
    const viewW = el.clientWidth;
    const x = playheadBeats * pps;
    const left = el.scrollLeft;
    if (x >= left && x <= left + viewW * 0.9) return;
    el.scrollLeft = Math.max(0, x - viewW * 0.2);
  }, [followPlayhead, transport.isPlaying, playheadBeats, pps]);

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
    if (tracksRef.current) {
      const sl = tracksRef.current.scrollLeft;
      if (rulerRef.current) rulerRef.current.scrollLeft = sl;
      if (arrangerLaneRef.current) arrangerLaneRef.current.scrollLeft = sl;
      setScrollLeft(sl);
    }
  }, []);

  // --- Ruler click-to-seek / drag-scrub / marquee-zoom (extracted hook) ---
  const { isScrubbing, handleRulerMouseDown, zoomRect } = useTimelineRuler({ pps, tracksRef, onMarqueeZoom: zoomToRange });

  // --- File drag-and-drop import (extracted hook) ---
  const { handleDrop } = useTimelineDrop({ pps, layout, tracksRef });

  // --- Context menu & clip operations (extracted hook) ---
  const {
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
  } = useTimelineClipOps({ clips });

  // --- Keyboard shortcuts (extracted hook) ---
  useTimelineKeyboard({
    handleDuplicateClip,
    pasteClipboard,
    setContextMenu,
    setEmptyContextMenu,
    tracksRef,
    setPps,
  });

  return (
    <div className="timeline-minimal">
      {/* Toolbar */}
      <div className="tl-toolbar" style={{ height: TOOLBAR_HEIGHT }}>
        <button className="tl-tb-btn" onClick={zoomOut} title="Zoom Out">−</button>
        <span className="tl-tb-label">{pps} px/beat</span>
        <button className="tl-tb-btn" onClick={zoomIn} title="Zoom In">+</button>
        <button className="tl-tb-btn" onClick={zoomFit} title="Fit All">⟷</button>
      </div>

      {/* Body */}
      <div className="tl-body" ref={bodyRef} onDragOver={(e) => e.preventDefault()} onDrop={handleDrop}>
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
            {zoomRect && (
              <div
                className="tl-zoom-rect"
                style={{ left: Math.min(zoomRect.x1, zoomRect.x2), width: Math.abs(zoomRect.x2 - zoomRect.x1) }}
              />
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

        {/* Arranger Lane */}
        <div ref={arrangerLaneRef} style={{ overflow: "hidden" }}>
          <ArrangerLane pps={pps} scrollLeft={scrollLeft} />
        </div>

        {/* Tracks area */}
        <div
          className="tl-tracks"
          ref={tracksRef}
          onScroll={onTracksScroll}
          style={dragCursor ? { cursor: dragCursor } : undefined}
          onContextMenu={(e) => {
            // Clicks inside the content area are handled by .tl-tracks-inner;
            // only the dead space below the last track reaches here.
            if ((e.target as HTMLElement).closest(".tl-tracks-inner")) return;
            e.preventDefault();
            setContextMenu(null);
            setEmptyContextMenu(null);
            setBelowMenu({ x: e.clientX, y: e.clientY });
          }}
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
              const row = rowAtY(layout, e.clientY - rect.top + el.scrollTop);
              // Map the visible row to its real track index (folder collapse
              // makes these differ) so Add MIDI Clip / Delete Track target the
              // track the user actually right-clicked.
              const trackIndex = visibleTracks[row] ? visibleTracks[row].index : row;
              setEmptyContextMenu({ x: e.clientX, y: e.clientY, beat, trackIndex });
            }}>
            {visibleTracks.map((track, idx) => {
              const trackClips = clipsByTrack.get(track.index) ?? [];
              const isTarget = dragState && idx === rowAtY(layout, dragState.mouseY);
              return (
                <div
                  key={track.index}
                  className={`tl-track-row${isTarget ? " tl-track-row--target" : ""} tl-track-row--${track.trackType === 2 ? "folder" : track.trackType === 1 ? "instrument" : "audio"}`}
                  style={{ top: layout.tops[idx], height: layout.heights[idx] }}
                >
                    {trackClips.map((clip) => {
                    const isDragging = dragState != null && dragSelectedIdsRef.current.has(clip.clipId);
                    const isTrimming = trimState?.clipId === clip.clipId;
                    const isSelected = selectedClipIds.has(clip.clipId);
                    const dispLeft = isTrimming ? trimState.currentStartBeat * pps : clip.startBeat * pps;
                    const dispWidth = isTrimming ? Math.max(4, trimState.currentDuration * pps) : Math.max(4, clip.durationBeats * pps);
                    const trackColor = trackColorByIndex.get(track.index) ?? "#38b2df";
                    return (
                      <div
                        key={clip.clipId}
                        data-clip-id={clip.clipId}
                        className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}${isSelected ? " tl-clip--selected" : ""}${clip.isGhost ? " tl-clip--ghost" : ""}${pendingTempIds.has(clip.clipId) ? " tl-clip--pending" : ""}`}
                        style={{ left: dispLeft, width: dispWidth, height: layout.heights[idx] - 8, top: 4, zIndex: isTrimming ? 3 : undefined, ["--clip-color" as string]: trackColor } as React.CSSProperties}
                        onClick={(e) => {
                          e.stopPropagation();
                          // Ensure focus is on the timeline (not a bottom-panel
                          // [tabindex] container) so the keyboard delete handler fires.
                          document.body.focus();
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
                          <WaveformCanvas
                            clip={clip}
                            width={Math.max(4, dispWidth)}
                            height={layout.heights[idx] - 8}
                            color={trackColor}
                            trimOverride={isTrimming ? {
                              offset: trimState.side === "left"
                                ? clip.offset + (trimState.currentStartBeat - clip.startBeat)
                                : clip.offset,
                              durationBeats: trimState.currentDuration,
                            } : undefined}
                          />
                        )}
                        {clip.isMidi && !clip.isGhost && (
                          <MidiThumbnailCanvas
                            clip={clip}
                            width={Math.max(4, dispWidth)}
                            height={layout.heights[idx] - 8}
                            color={trackColor}
                            trimOverride={isTrimming ? {
                              offset: trimState.side === "left"
                                ? clip.offset + (trimState.currentStartBeat - clip.startBeat)
                                : clip.offset,
                              durationBeats: trimState.currentDuration,
                            } : undefined}
                          />
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
                        <div className="clip-trim clip-trim-left" onMouseDown={(e) => { if (e.altKey) { handleFadeStart(e, clip, "in"); } else { handleTrimStart(e, clip, "left"); } }} onClick={(e) => e.stopPropagation()} />
                        <div className="clip-trim clip-trim-right" onMouseDown={(e) => { if (e.altKey) { handleFadeStart(e, clip, "out"); } else { handleTrimStart(e, clip, "right"); } }} onClick={(e) => e.stopPropagation()} />
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
              <div key={`paint-${i}`} className="tl-paint-tile tl-paint-tile--pending" style={{ left: tile.left, width: tile.width, top: tile.top, height: tile.height }} />
            ))}
            {dragState?.paintRepeat && paintCount > 0 && (
              <span className="tl-paint-badge" style={{ left: dragState.paintOriginBeat * pps + dragState.paintSpacing * paintCount * pps }}>+{paintCount}</span>
            )}

            {/* Drag preview */}
            {dragPreviewStyle && dragPreviewClip && (
              <div
                className={`tl-clip tl-ghost ${dragPreviewClip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}`}
                style={{ ...dragPreviewStyle, ["--clip-color" as string]: trackColorByIndex.get(dragPreviewClip.trackIndex) ?? "#38b2df", ...(dragPreviewClip.isMidi ? {} : { background: "transparent" }) } as React.CSSProperties}
              >
                {!dragPreviewClip.isMidi && (
                  <WaveformCanvas clip={dragPreviewClip} width={Math.max(4, dragPreviewClip.durationBeats * pps)} height={dragPreviewHeight} color={trackColorByIndex.get(dragPreviewClip.trackIndex) ?? "#38b2df"} />
                )}
                {dragPreviewClip.isMidi && (
                  <MidiThumbnailCanvas clip={dragPreviewClip} width={Math.max(4, dragPreviewClip.durationBeats * pps)} height={dragPreviewHeight} color={trackColorByIndex.get(dragPreviewClip.trackIndex) ?? "#38b2df"} />
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
          {visibleTracks.length > 0 && (
            <div className="tl-add-track" style={{ top: layout.total + 10 }}>
              <AddTrackMenu label="+" title="Add Track" />
            </div>
          )}
        </div>
        {visibleTracks.length === 0 && (
          <div className="tl-empty-overlay" style={{ top: RULER_HEIGHT }}>
            <div className="tl-empty-card">
              <div className="tl-empty-title">No tracks yet</div>
              <div className="tl-empty-hint">
                Add a track to start arranging — or drop an audio or MIDI file
                anywhere here to create one.
              </div>
              <AddTrackMenu label="+ Add Track" triggerClassName="tl-empty-btn" />
            </div>
          </div>
        )}
      </div>

      <TimelineContextMenu
        contextMenu={contextMenu}
        emptyContextMenu={emptyContextMenu}
        belowMenu={belowMenu}
        clips={clips}
        markers={markers}
        selectedClipIds={selectedClipIds}
        transport={transport}
        onClose={handleCloseContextMenu}
        onDeleteClip={handleDeleteClip}
        onDuplicateClip={handleDuplicateClip}
        onSplitClip={handleSplitClip}
        onBrowseClip={handleBrowseClip}
      />
      {browseClipTarget && (
        <PopUpBrowser
          context="clip"
          onSelect={handleBrowseSelect}
          onClose={() => setBrowseClipTarget(null)}
        />
      )}
    </div>
  );
}
