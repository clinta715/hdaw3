import { useMemo, useState, useCallback, useRef } from "react";
import { NoteSnapshot } from "../rpc/types";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";
import "./NoteGrid.css";

interface Props {
  notes: NoteSnapshot[];
  rpc: RpcClient;
  clipId: number | null;
}

interface NoteDragState {
  noteId: number;
  startPitch: number;
  startBeat: number;
  offsetX: number;
  offsetY: number;
  currentPitch: number;
  currentStart: number;
}

const KEY_HEIGHT = 8;
const PIXELS_PER_BEAT = 80;

function clamp(val: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, val));
}

export default function NoteGrid({ notes, rpc, clipId }: Props) {
  const [dragState, setDragState] = useState<NoteDragState | null>(null);
  const dragRef = useRef<NoteDragState | null>(null);
  dragRef.current = dragState;

  const rects = useMemo(() => {
    if (!notes.length) return [];
    let maxEnd = 0;
    for (const n of notes) {
      const end = n.startBeat + n.durationBeats;
      if (end > maxEnd) maxEnd = end;
    }
    const gridW = Math.max(maxEnd * PIXELS_PER_BEAT, 400);

    return notes.map((n) => {
      let x: number, y: number;
      if (dragState && dragState.noteId === n.noteId) {
        x = dragState.currentStart * PIXELS_PER_BEAT;
        y = (127 - dragState.currentPitch) * KEY_HEIGHT;
      } else {
        x = n.startBeat * PIXELS_PER_BEAT;
        y = (127 - n.pitch) * KEY_HEIGHT;
      }
      const w = Math.max(2, n.durationBeats * PIXELS_PER_BEAT);
      const h = KEY_HEIGHT - 1;
      return { x, y, w, h, noteId: n.noteId, vel: n.velocity };
    });
  }, [notes, dragState]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    setDragState((prev) => {
      if (!prev) return null;
      const newPitch = clamp(
        prev.startPitch - Math.round((e.clientY - prev.offsetY) / KEY_HEIGHT),
        0, 127
      );
      const newStart = Math.max(0,
        prev.startBeat + (e.clientX - prev.offsetX) / PIXELS_PER_BEAT
      );
      return { ...prev, currentPitch: newPitch, currentStart: newStart };
    });
  }, []);

  const finishDrag = useCallback(async () => {
    const state = dragRef.current;
    if (!state || clipId == null) {
      setDragState(null);
      return;
    }
    const { noteId, currentPitch, currentStart } = state;
    try {
      await rpc.call("project.setNotePitch", { noteId, pitch: currentPitch });
      await rpc.call("project.setNoteStart", { noteId, startBeat: currentStart });
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("note drag mutation failed", err);
    }
    setDragState(null);
  }, [rpc, clipId]);

  const cancelDrag = useCallback(() => {
    setDragState(null);
  }, []);

  return (
    <div
      className="note-grid"
      onMouseMove={handleMouseMove}
      onMouseUp={finishDrag}
      onMouseLeave={cancelDrag}
    >
      {rects.length === 0 && (
        <div className="ng-empty">No notes</div>
      )}
      {rects.map((r) => {
        const isDragging = dragState?.noteId === r.noteId;
        return (
          <div
            key={r.noteId}
            className={`ng-note${isDragging ? " ng-note--dragging" : ""}`}
            style={{
              left: r.x,
              top: r.y,
              width: r.w,
              height: r.h,
              opacity: 0.4 + r.vel * 0.6,
            }}
            onMouseDown={(e) => {
              if (isDragging || clipId == null) return;
              const note = notes.find((n) => n.noteId === r.noteId);
              if (!note) return;
              e.preventDefault();
              e.stopPropagation();
              setDragState({
                noteId: note.noteId,
                startPitch: note.pitch,
                startBeat: note.startBeat,
                offsetX: e.clientX,
                offsetY: e.clientY,
                currentPitch: note.pitch,
                currentStart: note.startBeat,
              });
            }}
          />
        );
      })}
    </div>
  );
}
