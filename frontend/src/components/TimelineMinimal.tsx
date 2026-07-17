import { useMemo, useState, useRef, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";
import "./TimelineMinimal.css";

const PIXELS_PER_BEAT = 40;
const TRACK_HEIGHT = 56;

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
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];
  const [dragState, setDragState] = useState<DragState | null>(null);
  const dragRef = useRef<DragState | null>(null);

  const updateDrag = useCallback((next: DragState | null) => {
    dragRef.current = next;
    setDragState(next);
  }, []);

  const clipsByTrack = useMemo(() => {
    const map = new Map<number, typeof clips>();
    for (const c of clips) {
      const group = map.get(c.trackIndex) ?? [];
      group.push(c);
      map.set(c.trackIndex, group);
    }
    return map;
  }, [clips]);

  const maxEnd = clips.reduce((max, c) => Math.max(max, c.startBeat + c.durationBeats), 4);
  const totalW = Math.max(maxEnd * PIXELS_PER_BEAT, 400);

  const handleClipMouseDown = useCallback(
    (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number) => {
      e.preventDefault();
      const clipEl = e.currentTarget as HTMLElement;
      const rect = clipEl.getBoundingClientRect();
      const d: DragState = {
        clipId,
        startTrackIndex: trackIndex,
        startBeat,
        offsetX: e.clientX - rect.left,
        offsetY: e.clientY - rect.top,
        mouseX: e.clientX,
        mouseY: e.clientY,
      };
      updateDrag(d);
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
    const containerEl = document.querySelector(".tl-tracks");
    if (!containerEl) return;
    const containerRect = containerEl.getBoundingClientRect();
    const relX = d.mouseX - containerRect.left;
    const relY = d.mouseY - containerRect.top;
    const newStart = Math.max(0, (relX - d.offsetX) / PIXELS_PER_BEAT);
    const newTrackIndex = Math.min(
      Math.max(0, Math.floor(relY / TRACK_HEIGHT)),
      tracks.length - 1
    );
    if (newTrackIndex !== d.startTrackIndex || Math.abs(newStart - d.startBeat) > 0.01) {
      rpc.call("project.moveClip", { clipId: d.clipId, newTrackIndex, newStart });
    }
  }, [tracks.length, updateDrag]);

  const handleMouseLeave = useCallback(() => {
    updateDrag(null);
  }, [updateDrag]);

  let ghostStyle: React.CSSProperties | undefined;
  let ghostClip: (typeof clips)[0] | undefined;
  if (dragState) {
    const containerEl = document.querySelector(".tl-tracks");
    if (containerEl) {
      const containerRect = containerEl.getBoundingClientRect();
      const relX = dragState.mouseX - containerRect.left;
      const relY = dragState.mouseY - containerRect.top;
      const newStart = Math.max(0, (relX - dragState.offsetX) / PIXELS_PER_BEAT);
      const newTrackIdx = Math.min(
        Math.max(0, Math.floor(relY / TRACK_HEIGHT)),
        tracks.length - 1
      );
      const originalClip = clips.find((c) => c.clipId === dragState.clipId);
      if (originalClip) {
        ghostClip = originalClip;
        ghostStyle = {
          left: newStart * PIXELS_PER_BEAT,
          width: Math.max(4, originalClip.durationBeats * PIXELS_PER_BEAT),
          height: TRACK_HEIGHT - 8,
          top: newTrackIdx * TRACK_HEIGHT + 4,
        };
      }
    }
  }

  return (
    <div className="timeline-minimal">
      {tracks.length === 0 && (
        <div className="tl-empty">No tracks</div>
      )}
      <div
        className="tl-tracks"
        style={{ height: tracks.length * TRACK_HEIGHT }}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseLeave}
      >
        {tracks.map((track, idx) => {
          const trackClips = clipsByTrack.get(track.index) ?? [];
          const isTarget = dragState && idx === Math.min(
            Math.max(0, Math.floor(dragState.mouseY / TRACK_HEIGHT)),
            tracks.length - 1
          );
          return (
            <div
              key={track.index}
              className={`tl-track-row${isTarget ? " tl-track-row--target" : ""}`}
              style={{
                top: idx * TRACK_HEIGHT,
                height: TRACK_HEIGHT,
              }}
            >
              {trackClips.map((clip) => {
                const isDragging = dragState?.clipId === clip.clipId;
                return (
                  <div
                    key={clip.clipId}
                    className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}`}
                    style={{
                      left: clip.startBeat * PIXELS_PER_BEAT,
                      width: Math.max(4, clip.durationBeats * PIXELS_PER_BEAT),
                      height: TRACK_HEIGHT - 8,
                      top: 4,
                    }}
                    onMouseDown={(e) =>
                      handleClipMouseDown(e, clip.clipId, idx, clip.startBeat)
                    }
                  >
                    <span className="tl-clip-name">{clip.name ?? `Clip ${clip.clipId}`}</span>
                  </div>
                );
              })}
            </div>
          );
        })}
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
  );
}
