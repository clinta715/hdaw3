import { useCallback } from "react";
import { NoteSnapshot } from "../../rpc/types";
import { useUiStore } from "../../store/uiStore";
import { NoteDragState, NoteResizeState } from "./noteGridTypes";
import { KEY_HEIGHT } from "./noteGridConstants";

interface UseNoteGridNoteMouseDownOptions {
  clipId: number | null;
  selectedNoteIds: Set<number>;
  setSelectedNoteIds: (ids: Set<number> | ((prev: Set<number>) => Set<number>)) => void;
  lastClickedNoteRef: React.MutableRefObject<number | null>;
  noteMap: Map<number, NoteSnapshot>;
  notes: NoteSnapshot[];
  setDragState: React.Dispatch<React.SetStateAction<NoteDragState | null>>;
  setResizeState: React.Dispatch<React.SetStateAction<NoteResizeState | null>>;
}

export function useNoteGridNoteMouseDown({
  clipId,
  selectedNoteIds,
  setSelectedNoteIds,
  lastClickedNoteRef,
  noteMap,
  notes,
  setDragState,
  setResizeState,
}: UseNoteGridNoteMouseDownOptions) {
  return useCallback(
    (e: React.MouseEvent, note: NoteSnapshot, isDragging: boolean, isResizing: boolean) => {
      if (clipId == null) return;
      if (isDragging || isResizing) return;
      e.preventDefault();
      e.stopPropagation();

      if (e.ctrlKey || e.metaKey) {
        setSelectedNoteIds((prev) => {
          const next = new Set(prev);
          if (next.has(note.noteId)) next.delete(note.noteId);
          else next.add(note.noteId);
          return next;
        });
        lastClickedNoteRef.current = note.noteId;
        return;
      }

      if (e.shiftKey) {
        const lastNote =
          lastClickedNoteRef.current != null
            ? noteMap.get(lastClickedNoteRef.current)
            : null;
        if (lastNote && lastNote.pitch === note.pitch) {
          const minBeat = Math.min(lastNote.startBeat, note.startBeat);
          const maxBeat = Math.max(lastNote.startBeat, note.startBeat);
          const rangeIds = new Set(selectedNoteIds);
          for (const n of notes) {
            if (
              n.pitch === note.pitch &&
              n.startBeat >= minBeat &&
              n.startBeat <= maxBeat
            ) {
              rangeIds.add(n.noteId);
            }
          }
          setSelectedNoteIds(rangeIds);
          lastClickedNoteRef.current = note.noteId;
          return;
        }
        setSelectedNoteIds((prev) => new Set(prev).add(note.noteId));
        lastClickedNoteRef.current = note.noteId;
        return;
      }

      if (!selectedNoteIds.has(note.noteId)) {
        setSelectedNoteIds(new Set([note.noteId]));
      }
      lastClickedNoteRef.current = note.noteId;

      const noteRect = (
        e.currentTarget as HTMLElement
      ).getBoundingClientRect();
      const localX = e.clientX - noteRect.left;
      useUiStore
        .getState()
        .setStatusHint(
          "Draw: click \u00b7 Drag: move/resize note \u00b7 Alt+drag: quick-draw row"
        );
      if (localX > noteRect.width - 6) {
        setResizeState({
          noteId: note.noteId,
          startX: e.clientX,
          initialDuration: note.durationBeats,
          currentDuration: note.durationBeats,
        });
      } else {
        const memberIds = selectedNoteIds.has(note.noteId)
          ? Array.from(selectedNoteIds)
          : [note.noteId];
        const members = memberIds
          .map((id) => {
            const n = noteMap.get(id);
            return n
              ? { noteId: id, startPitch: n.pitch, startBeat: n.startBeat }
              : null;
          })
          .filter(
            (m): m is { noteId: number; startPitch: number; startBeat: number } =>
              m != null
          );
        const anchorIndex = Math.max(
          0,
          members.findIndex((m) => m.noteId === note.noteId)
        );
        const minPitch = Math.min(...members.map((m) => m.startPitch));
        const maxPitch = Math.max(...members.map((m) => m.startPitch));
        const minBeat = Math.min(...members.map((m) => m.startBeat));
        setDragState({
          members,
          anchorIndex,
          offsetX: e.clientX,
          offsetY: e.clientY,
          minPitch,
          maxPitch,
          minBeat,
          pitchDelta: 0,
          beatDelta: 0,
        });
      }
    },
    [
      clipId,
      selectedNoteIds,
      setSelectedNoteIds,
      lastClickedNoteRef,
      noteMap,
      notes,
      setDragState,
      setResizeState,
    ]
  );
}
