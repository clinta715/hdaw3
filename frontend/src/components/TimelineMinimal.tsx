import { useState, useMemo, useRef, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
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
}

export default function TimelineMinimal() {
  const [pps, setPps] = useState(DEFAULT_PPS);

  const snapshot = useProjectStore((s) => s.snapshot);
  const transport = useTransportStore((s) => s.transport);
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

  // --- Loop drag state ---
  const [loopDrag, setLoopDrag] = useState<"start" | "end" | null>(null);
  const [dragBeat, setDragBeat] = useState(0);
  const dragBeatRef = useRef(0);

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

  // --- Clip drag handlers (Phase 3) ---
  const handleClipMouseDown = useCallback(
    (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number) => {
      e.preventDefault();
      const el = e.currentTarget as HTMLElement;
      const r = el.getBoundingClientRect();
      updateDrag({ clipId, startTrackIndex: trackIndex, startBeat, offsetX: e.clientX - r.left, offsetY: e.clientY - r.top, mouseX: e.clientX, mouseY: e.clientY });
    },
    [updateDrag]
  );

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    const d = dragRef.current;
    if (!d) return;
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
      rpc.call("project.moveClip", { clipId: d.clipId, newTrackIndex, newStart }).catch(() => {});
    }
  }, [pps, tracks.length, updateDrag]);

  const handleMouseLeave = useCallback(() => updateDrag(null), [updateDrag]);

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
        <div className="tl-ruler" ref={rulerRef} style={{ height: RULER_HEIGHT }}>
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
            onClick={() => useUiStore.getState().selectClip(null)}>
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
                    return (
                      <div
                        key={clip.clipId}
                        className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}`}
                        style={{ left: clip.startBeat * pps, width: Math.max(4, clip.durationBeats * pps), height: TRACK_HEIGHT - 8, top: 4 }}
                        onClick={(e) => { e.stopPropagation(); useUiStore.getState().selectClip(clip.clipId); }}
                        onMouseDown={(e) => handleClipMouseDown(e, clip.clipId, idx, clip.startBeat)}
                      >
                        <span className="tl-clip-name">{clip.name ?? `Clip ${clip.clipId}`}</span>
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
                style={ghostStyle}
              >
                <span className="tl-clip-name">{ghostClip.name ?? `Clip ${ghostClip.clipId}`}</span>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
