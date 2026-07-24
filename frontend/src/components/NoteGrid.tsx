import { useMemo, useState, useCallback, useRef, useEffect } from "react";
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
  onHorizontalScroll?: (scrollLeft: number) => void;
  selectedNoteIds?: Set<number>;
  onSelectionChange?: (ids: Set<number>) => void;
  chordShape?: number[];
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

interface ContextMenuState {
  x: number;
  y: number;
  noteId: number | null;
}

const KEY_HEIGHT = 8;
const PIXELS_PER_BEAT = 80;
const TOTAL_KEY_AREA = 128 * KEY_HEIGHT;

function clamp(val: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, val));
}

let noteClipboard: NoteSnapshot[] = [];

export default function NoteGrid({
  notes,
  rpc,
  clipId,
  onVerticalScroll,
  onHorizontalScroll,
  selectedNoteIds: externalSelectedIds,
  onSelectionChange,
  chordShape,
}: Props) {
  const [dragState, setDragState] = useState<NoteDragState | null>(null);
  const dragRef = useRef<NoteDragState | null>(null);
  dragRef.current = dragState;

  const [internalSelectedIds, setInternalSelectedIds] = useState<Set<number>>(new Set());
  const selectedNoteIds = externalSelectedIds ?? internalSelectedIds;
  const setSelectedNoteIds = useCallback(
    (ids: Set<number> | ((prev: Set<number>) => Set<number>)) => {
      const next = typeof ids === "function" ? ids(selectedNoteIds) : ids;
      if (onSelectionChange) onSelectionChange(next);
      else setInternalSelectedIds(next);
    },
    [onSelectionChange, selectedNoteIds]
  );

  const [resizeState, setResizeState] = useState<NoteResizeState | null>(null);
  const resizeRef = useRef<NoteResizeState | null>(null);
  resizeRef.current = resizeState;

  const [contextMenu, setContextMenu] = useState<ContextMenuState | null>(null);

  const gridRef = useRef<HTMLDivElement>(null);
  const lastClickedNoteRef = useRef<number | null>(null);

  const noteMap = useMemo(() => {
    const m = new Map<number, NoteSnapshot>();
    for (const n of notes) m.set(n.noteId, n);
    return m;
  }, [notes]);

  const rects = useMemo(() => {
    if (!notes.length) return [];
    let maxEnd = 0;
    for (const n of notes) {
      const end = n.startBeat + n.durationBeats;
      if (end > maxEnd) maxEnd = end;
    }

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

  const handleMouseMove = useCallback((e: globalThis.MouseEvent) => {
    const { snapEnabled, snapDivision } = useUiStore.getState();

    setResizeState((prev) => {
      if (!prev) return null;
      const deltaX = e.clientX - prev.startX;
      const rawDuration = Math.max(0.03125, prev.initialDuration + deltaX / PIXELS_PER_BEAT);
      const newDuration = snapEnabled ? Math.max(0.03125, snapToGrid(rawDuration, snapDivision)) : rawDuration;
      return { ...prev, currentDuration: newDuration };
    });

    setDragState((prev) => {
      if (!prev) return null;
      const newPitch = clamp(
        prev.startPitch - Math.round((e.clientY - prev.offsetY) / KEY_HEIGHT),
        0, 127
      );
      const rawStart = Math.max(0,
        prev.startBeat + (e.clientX - prev.offsetX) / PIXELS_PER_BEAT
      );
      const newStart = snapEnabled ? snapToGrid(rawStart, snapDivision) : rawStart;
      return { ...prev, currentPitch: newPitch, currentStart: newStart };
    });
  }, []);

  const handleMouseUp = useCallback(async () => {
    const drag = dragRef.current;
    const resize = resizeRef.current;

    // Optimistic local update: write the committed pitch/start/duration into
    // the notesByClip store BEFORE clearing the drag/resize preview, so the
    // note doesn't snap back to its pre-gesture position for the round-trip
    // until syncNotes returns.
    if ((drag || resize) && clipId != null) {
      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId);
        if (!arr) return {};
        return {
          notesByClip: new Map(s.notesByClip).set(
            clipId,
            arr.map((n) => {
              if (drag && n.noteId === drag.noteId) {
                return { ...n, pitch: drag.currentPitch, startBeat: drag.currentStart };
              }
              if (resize && n.noteId === resize.noteId) {
                return { ...n, durationBeats: resize.currentDuration };
              }
              return n;
            })
          ),
        };
      });
    }

    try {
      if (drag && clipId != null) {
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const snappedStart = snapEnabled ? snapToGrid(drag.currentStart, snapDivision) : drag.currentStart;
        await rpc.call("project.setNotePitch", { noteId: drag.noteId, pitch: drag.currentPitch });
        await rpc.call("project.setNoteStart", { noteId: drag.noteId, startBeat: snappedStart });
      }
      if (resize && clipId != null) {
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const snappedDuration = snapEnabled ? Math.max(0.03125, snapToGrid(resize.currentDuration, snapDivision)) : resize.currentDuration;
        await rpc.call("project.setNoteDuration", { noteId: resize.noteId, durationBeats: snappedDuration });
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

  // Window-level drag listeners installed at note mousedown and removed on
  // release. Element-level handlers (onMouseMove/Up/Leave on .note-grid) miss
  // events once the cursor leaves the grid — the old onMouseLeave={cancelAll}
  // silently abandoned an in-flight drag without committing it.
  useEffect(() => {
    const move = (e: globalThis.MouseEvent) => handleMouseMove(e);
    const up = () => {
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
      handleMouseUp();
    };
    // Only active while a drag/resize is in progress.
    if (dragState || resizeState) {
      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", up);
      return () => {
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
      };
    }
  }, [dragState, resizeState, handleMouseMove, handleMouseUp]);

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
    
    // Optimistic: add note(s) to local store immediately
    const newNotes: NoteSnapshot[] = [{
      noteId: Date.now(),
      pitch,
      velocity: 100,
      startBeat,
      durationBeats: 0.25,
    }];
    if (chordShape && chordShape.length > 0) {
      for (const interval of chordShape.slice(1)) {
        newNotes.push({
          noteId: Date.now() + interval,
          pitch: pitch + interval,
          velocity: 100,
          startBeat,
          durationBeats: 0.25,
        });
      }
    }
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId) ?? [];
      return {
        notesByClip: new Map(s.notesByClip).set(clipId, [...arr, ...newNotes]),
      };
    });
    
    try {
      await rpc.call("project.addNote", { clipId, pitch, startBeat, durationBeats: 0.25, velocity: 100 });
      if (chordShape && chordShape.length > 0) {
        for (const interval of chordShape.slice(1)) {
          await rpc.call("project.addNote", {
            clipId,
            pitch: pitch + interval,
            startBeat,
            durationBeats: 0.25,
            velocity: 100,
          });
        }
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("note creation failed", err);
    }
  }, [rpc, clipId, chordShape]);

  const deleteSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;
    
    // Optimistic: remove notes from local store immediately
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.filter((n) => !selectedNoteIds.has(n.noteId))
        ),
      };
    });
    setSelectedNoteIds(new Set());
    
    try {
      if (selectedNoteIds.size > 1) {
        await rpc.call("project.beginTransaction", { name: "delete notes" });
      }
      for (const noteId of selectedNoteIds) {
        await rpc.call("project.removeNote", { noteId });
      }
      if (selectedNoteIds.size > 1) {
        await rpc.call("project.endTransaction");
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("note deletion failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, setSelectedNoteIds]);

  const transposeSelected = useCallback(
    async (semitones: number) => {
      if (clipId == null || selectedNoteIds.size === 0) return;
      
      // Optimistic: update pitches in local store immediately
      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId);
        if (!arr) return {};
        return {
          notesByClip: new Map(s.notesByClip).set(
            clipId,
            arr.map((n) =>
              selectedNoteIds.has(n.noteId)
                ? { ...n, pitch: clamp(n.pitch + semitones, 0, 127) }
                : n
            )
          ),
        };
      });
      
      try {
        for (const noteId of selectedNoteIds) {
          const note = noteMap.get(noteId);
          if (!note) continue;
          const newPitch = clamp(note.pitch + semitones, 0, 127);
          await rpc.call("project.setNotePitch", { noteId, pitch: newPitch });
        }
        useProjectStore.getState().syncNotes(rpc, clipId);
      } catch (err) {
        console.warn("transpose failed", err);
      }
    },
    [selectedNoteIds, rpc, clipId, noteMap]
  );

  const quantizeSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    if (!snapEnabled) return;
    
    // Optimistic: update start times in local store immediately
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.map((n) =>
            selectedNoteIds.has(n.noteId)
              ? { ...n, startBeat: snapToGrid(n.startBeat, snapDivision) }
              : n
          )
        ),
      };
    });
    
    try {
      for (const noteId of selectedNoteIds) {
        const note = noteMap.get(noteId);
        if (!note) continue;
        const snapped = snapToGrid(note.startBeat, snapDivision);
        await rpc.call("project.setNoteStart", { noteId, startBeat: snapped });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("quantize failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, noteMap]);

  const humanizeSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;
    
    // Optimistic: update start times and velocities in local store immediately
    const offsets = new Map<number, { beatOffset: number; velOffset: number }>();
    for (const noteId of selectedNoteIds) {
      offsets.set(noteId, {
        beatOffset: (Math.random() - 0.5) * 0.06,
        velOffset: Math.round((Math.random() - 0.5) * 10),
      });
    }
    
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.map((n) => {
            const offset = offsets.get(n.noteId);
            if (!offset) return n;
            return {
              ...n,
              startBeat: Math.max(0, n.startBeat + offset.beatOffset),
              velocity: clamp(n.velocity + offset.velOffset, 1, 127),
            };
          })
        ),
      };
    });
    
    try {
      for (const noteId of selectedNoteIds) {
        const note = noteMap.get(noteId);
        if (!note) continue;
        const offset = offsets.get(noteId)!;
        const newStart = Math.max(0, note.startBeat + offset.beatOffset);
        const newVel = clamp(note.velocity + offset.velOffset, 1, 127);
        await rpc.call("project.setNoteStart", { noteId, startBeat: newStart });
        await rpc.call("project.setNoteVelocity", { noteId, velocity: newVel });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("humanize failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, noteMap]);

  const copySelected = useCallback(() => {
    if (selectedNoteIds.size === 0) return;
    noteClipboard = notes.filter((n) => selectedNoteIds.has(n.noteId)).map((n) => ({ ...n }));
  }, [selectedNoteIds, notes]);

  const cutSelected = useCallback(async () => {
    copySelected();
    await deleteSelected();
  }, [copySelected, deleteSelected]);

  const pasteAtScroll = useCallback(async () => {
    if (clipId == null || noteClipboard.length === 0) return;
    const gridEl = gridRef.current;
    const scrollBeat = gridEl ? gridEl.scrollLeft / PIXELS_PER_BEAT : 0;
    const minBeat = Math.min(...noteClipboard.map((n) => n.startBeat));
    
    // Optimistic: add pasted notes to local store immediately
    const pastedNotes: NoteSnapshot[] = noteClipboard.map((n, i) => ({
      ...n,
      noteId: Date.now() + i,
      startBeat: n.startBeat - minBeat + scrollBeat,
    }));
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId) ?? [];
      return {
        notesByClip: new Map(s.notesByClip).set(clipId, [...arr, ...pastedNotes]),
      };
    });
    
    try {
      for (const n of noteClipboard) {
        const startBeat = n.startBeat - minBeat + scrollBeat;
        await rpc.call("project.addNote", {
          clipId,
          pitch: n.pitch,
          startBeat,
          durationBeats: n.durationBeats,
          velocity: n.velocity,
        });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("paste failed", err);
    }
  }, [rpc, clipId]);

  const selectAll = useCallback(() => {
    setSelectedNoteIds(new Set(notes.map((n) => n.noteId)));
  }, [notes, setSelectedNoteIds]);

  const handleKeyDown = useCallback(
    async (e: React.KeyboardEvent) => {
      if (clipId == null) return;
      setContextMenu(null);

      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        await deleteSelected();
        return;
      }
      if (e.key === "ArrowUp") {
        e.preventDefault();
        await transposeSelected(e.ctrlKey ? 12 : 1);
        return;
      }
      if (e.key === "ArrowDown") {
        e.preventDefault();
        await transposeSelected(e.ctrlKey ? -12 : -1);
        return;
      }
      if (e.code === "KeyQ") {
        e.preventDefault();
        await quantizeSelected();
        return;
      }
      if (e.code === "KeyH") {
        e.preventDefault();
        await humanizeSelected();
        return;
      }
      if (e.ctrlKey || e.metaKey) {
        if (e.code === "KeyC") {
          e.preventDefault();
          copySelected();
          return;
        }
        if (e.code === "KeyX") {
          e.preventDefault();
          await cutSelected();
          return;
        }
        if (e.code === "KeyV") {
          e.preventDefault();
          await pasteAtScroll();
          return;
        }
        if (e.code === "KeyA") {
          e.preventDefault();
          selectAll();
          return;
        }
      }
    },
    [clipId, deleteSelected, transposeSelected, quantizeSelected, humanizeSelected, copySelected, cutSelected, pasteAtScroll, selectAll]
  );

  const handleGridClick = useCallback(
    (e: React.MouseEvent) => {
      if (!(e.target as HTMLElement).closest(".ng-note")) {
        setSelectedNoteIds(new Set());
        lastClickedNoteRef.current = null;
      }
      setContextMenu(null);
    },
    [setSelectedNoteIds]
  );

  const handleContextMenu = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      const noteEl = (e.target as HTMLElement).closest(".ng-note");
      const noteId = noteEl ? Number(noteEl.getAttribute("data-note-id")) : null;
      if (noteId != null && !selectedNoteIds.has(noteId)) {
        setSelectedNoteIds(new Set([noteId]));
      }
      setContextMenu({ x: e.clientX, y: e.clientY, noteId });
    },
    [selectedNoteIds, setSelectedNoteIds]
  );

  const handleScroll = useCallback(() => {
    if (gridRef.current) {
      if (onVerticalScroll) onVerticalScroll(gridRef.current.scrollTop);
      if (onHorizontalScroll) onHorizontalScroll(gridRef.current.scrollLeft);
    }
  }, [onVerticalScroll, onHorizontalScroll]);

  const contextActions = useMemo(() => {
    if (!contextMenu) return [];
    return [
      { label: "Quantize", shortcut: "Q", action: quantizeSelected },
      { label: "Humanize", shortcut: "H", action: humanizeSelected },
      { label: "Transpose Up +1", shortcut: "↑", action: () => transposeSelected(1) },
      { label: "Transpose Down -1", shortcut: "↓", action: () => transposeSelected(-1) },
      { label: "Transpose Up Octave", shortcut: "Ctrl+↑", action: () => transposeSelected(12) },
      { label: "Transpose Down Octave", shortcut: "Ctrl+↓", action: () => transposeSelected(-12) },
      { label: "Delete Selected", shortcut: "Del", action: deleteSelected },
    ];
  }, [contextMenu, quantizeSelected, humanizeSelected, transposeSelected, deleteSelected]);

  return (
    <div
      className="note-grid"
      ref={gridRef}
      onDoubleClick={handleDoubleClick}
      onClick={handleGridClick}
      onContextMenu={handleContextMenu}
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
        const isSelected = selectedNoteIds.has(r.noteId);
        const note = noteMap.get(r.noteId);
        return (
          <div
            key={r.noteId}
            data-note-id={r.noteId}
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

              if (e.shiftKey && lastClickedNoteRef.current != null) {
                const lastNote = noteMap.get(lastClickedNoteRef.current);
                if (lastNote && lastNote.pitch === note.pitch) {
                  const minBeat = Math.min(lastNote.startBeat, note.startBeat);
                  const maxBeat = Math.max(lastNote.startBeat, note.startBeat);
                  const rangeIds = new Set(selectedNoteIds);
                  for (const n of notes) {
                    if (n.pitch === note.pitch && n.startBeat >= minBeat && n.startBeat <= maxBeat) {
                      rangeIds.add(n.noteId);
                    }
                  }
                  setSelectedNoteIds(rangeIds);
                  lastClickedNoteRef.current = note.noteId;
                  return;
                }
              }

              if (!selectedNoteIds.has(note.noteId)) {
                setSelectedNoteIds(new Set([note.noteId]));
              }
              lastClickedNoteRef.current = note.noteId;

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

      {contextMenu && (
        <div
          className="ng-context-menu"
          style={{ left: contextMenu.x, top: contextMenu.y }}
          onMouseDown={(e) => e.stopPropagation()}
        >
          {contextActions.map((a) => (
            <button
              key={a.label}
              className="ng-context-item"
              onMouseDown={(e) => {
                e.stopPropagation();
                setContextMenu(null);
                a.action();
              }}
            >
              <span>{a.label}</span>
              <span className="ng-context-shortcut">{a.shortcut}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
