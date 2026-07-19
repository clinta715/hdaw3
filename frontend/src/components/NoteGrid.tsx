import { useMemo, useState, useCallback, useRef } from "react";
import { NoteSnapshot } from "../rpc/types";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "./snapUtils";
import "./NoteGrid.css";

interface Props {
  notes: NoteSnapshot[];
  rpc: RpcClient;
  clipId: number | null;
  onVerticalScroll?: (scrollTop: number) => void;
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

interface NoteResizeState {
  noteId: number;
  startX: number;
  initialDuration: number;
  currentDuration: number;
}

const KEY_HEIGHT = 8;
const PIXELS_PER_BEAT = 80;
const TOTAL_KEY_AREA = 128 * KEY_HEIGHT;

function clamp(val: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, val));
}

export default function NoteGrid({ notes, rpc, clipId, onVerticalScroll }: Props) {
  const [dragState, setDragState] = useState<NoteDragState | null>(null);
  const dragRef = useRef<NoteDragState | null>(null);
  dragRef.current = dragState;

  const [selectedNoteId, setSelectedNoteId] = useState<number | null>(null);

  const [resizeState, setResizeState] = useState<NoteResizeState | null>(null);
  const resizeRef = useRef<NoteResizeState | null>(null);
  resizeRef.current = resizeState;

  const gridRef = useRef<HTMLDivElement>(null);

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
      let w: number;

      if (dragState && dragState.noteId === n.noteId) {
        x = dragState.currentStart * PIXELS_PER_BEAT;
        y = (127 - dragState.currentPitch) * KEY_HEIGHT;
        w = Math.max(2, n.durationBeats * PIXELS_PER_BEAT);
      } else if (resizeState && resizeState.noteId === n.noteId) {
        x = n.startBeat * PIXELS_PER_BEAT;
        y = (127 - n.pitch) * KEY_HEIGHT;
        w = Math.max(2, resizeState.currentDuration * PIXELS_PER_BEAT);
      } else {
        x = n.startBeat * PIXELS_PER_BEAT;
        y = (127 - n.pitch) * KEY_HEIGHT;
        w = Math.max(2, n.durationBeats * PIXELS_PER_BEAT);
      }

      const h = KEY_HEIGHT - 1;
      return { x, y, w, h, noteId: n.noteId, vel: n.velocity };
    });
  }, [notes, dragState, resizeState]);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    setResizeState((prev) => {
      if (!prev) return null;
      const deltaX = e.clientX - prev.startX;
      const newDuration = Math.max(0.03125, prev.initialDuration + deltaX / PIXELS_PER_BEAT);
      return { ...prev, currentDuration: newDuration };
    });

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

  const handleMouseUp = useCallback(async () => {
    const drag = dragRef.current;
    const resize = resizeRef.current;
    try {
      if (drag && clipId != null) {
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const snappedStart = snapEnabled ? snapToGrid(drag.currentStart, snapDivision) : drag.currentStart;
        await rpc.call("project.setNotePitch", { noteId: drag.noteId, pitch: drag.currentPitch });
        await rpc.call("project.setNoteStart", { noteId: drag.noteId, startBeat: snappedStart });
      }
      if (resize && clipId != null) {
        await rpc.call("project.setNoteDuration", { noteId: resize.noteId, durationBeats: resize.currentDuration });
      }
      if ((drag || resize) && clipId != null) {
        useProjectStore.getState().syncNotes(rpc, clipId);
      }
    } catch (err) {
      console.warn("note interaction failed", err);
    }
    setDragState(null);
    setResizeState(null);
  }, [rpc, clipId]);

  const cancelAll = useCallback(() => {
    setDragState(null);
    setResizeState(null);
  }, []);

  const handleDoubleClick = useCallback(async (e: React.MouseEvent) => {
    if (clipId == null) return;
    if ((e.target as HTMLElement).closest(".ng-note")) return;

    const gridEl = gridRef.current;
    if (!gridEl) return;
    const rect = gridEl.getBoundingClientRect();
    const x = e.clientX - rect.left + gridEl.scrollLeft;
    const y = e.clientY - rect.top + gridEl.scrollTop;
    const pitch = clamp(127 - Math.floor(y / KEY_HEIGHT), 0, 127);
    const rawBeat = x / PIXELS_PER_BEAT;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const startBeat = snapEnabled ? snapToGrid(rawBeat, snapDivision) : rawBeat;
    try {
      await rpc.call("project.addNote", { clipId, pitch, startBeat, duration: 0.25, velocity: 100 });
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("note creation failed", err);
    }
  }, [rpc, clipId]);

  const handleKeyDown = useCallback(async (e: React.KeyboardEvent) => {
    if (selectedNoteId == null || clipId == null) return;
    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      try {
        await rpc.call("project.removeNote", { noteId: selectedNoteId });
        setSelectedNoteId(null);
        useProjectStore.getState().syncNotes(rpc, clipId);
      } catch (err) {
        console.warn("note deletion failed", err);
      }
    }
  }, [selectedNoteId, rpc, clipId]);

  const handleGridClick = useCallback((e: React.MouseEvent) => {
    if (!(e.target as HTMLElement).closest(".ng-note")) {
      setSelectedNoteId(null);
    }
  }, []);

  const handleScroll = useCallback(() => {
    if (onVerticalScroll && gridRef.current) {
      onVerticalScroll(gridRef.current.scrollTop);
    }
  }, [onVerticalScroll]);

  return (
    <div
      className="note-grid"
      ref={gridRef}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={cancelAll}
      onDoubleClick={handleDoubleClick}
      onClick={handleGridClick}
      onScroll={handleScroll}
      tabIndex={0}
      onKeyDown={handleKeyDown}
    >
      <div style={{ height: TOTAL_KEY_AREA, pointerEvents: "none" }} />
      {rects.length === 0 && (
        <div className="ng-empty">No notes</div>
      )}
      {rects.map((r) => {
        const isDragging = dragState?.noteId === r.noteId;
        const isResizing = resizeState?.noteId === r.noteId;
        const isSelected = selectedNoteId === r.noteId;
        const note = notes.find((n) => n.noteId === r.noteId);
        return (
          <div
            key={r.noteId}
            className={`ng-note${isDragging ? " ng-note--dragging" : ""}${isSelected ? " ng-note--selected" : ""}${isResizing ? " ng-note--resizing" : ""}`}
            style={{
              left: r.x,
              top: r.y,
              width: r.w,
              height: r.h,
              opacity: 0.4 + r.vel * 0.6,
            }}
            onMouseDown={(e) => {
              if (clipId == null || !note) return;
              if (isDragging || isResizing) return;
              e.preventDefault();
              e.stopPropagation();

              setSelectedNoteId(note.noteId);

              const noteRect = (e.currentTarget as HTMLElement).getBoundingClientRect();
              const localX = e.clientX - noteRect.left;
              if (localX > noteRect.width - 6) {
                setResizeState({
                  noteId: note.noteId,
                  startX: e.clientX,
                  initialDuration: note.durationBeats,
                  currentDuration: note.durationBeats,
                });
              } else {
                setDragState({
                  noteId: note.noteId,
                  startPitch: note.pitch,
                  startBeat: note.startBeat,
                  offsetX: e.clientX,
                  offsetY: e.clientY,
                  currentPitch: note.pitch,
                  currentStart: note.startBeat,
                });
              }
            }}
          />
        );
      })}
    </div>
  );
}
