import { useMemo, useState, useCallback, useRef, useEffect } from "react";
import { NoteSnapshot } from "../../rpc/types";
import { RpcClient } from "../../rpc/client";
import "../NoteGrid.css";
import { useAutoScroll } from "../../hooks/useAutoScroll";
import { Props, NoteDragState, NoteResizeState, ContextMenuState } from "./noteGridTypes";
import { KEY_HEIGHT, TOTAL_KEY_AREA } from "./noteGridConstants";
import { useNoteGridDrag } from "./useNoteGridDrag";
import { useNoteGridInteractions } from "./useNoteGridInteractions";
import { useNoteGridMarquee } from "./useNoteGridMarquee";
import { useNoteGridNoteMouseDown } from "./useNoteGridNoteMouseDown";

export default function NoteGrid({
  notes,
  rpc,
  clipId,
  pixelsPerBeat,
  onVerticalScroll,
  onHorizontalScroll,
  onZoom,
  selectedNoteIds: externalSelectedIds,
  onSelectionChange,
  chordShape,
  quantizeStrength = 100,
  swing = 0,
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
  const autoScroll = useAutoScroll(gridRef);
  const lastClickedNoteRef = useRef<number | null>(null);
  const ppbRef = useRef(pixelsPerBeat); ppbRef.current = pixelsPerBeat;
  const notesRef = useRef(notes); notesRef.current = notes;
  const selectedRef = useRef(selectedNoteIds); selectedRef.current = selectedNoteIds;

  const noteMap = useMemo(() => {
    const m = new Map<number, NoteSnapshot>();
    for (const n of notes) m.set(n.noteId, n);
    return m;
  }, [notes]);

  const { marquee, marqueeJustCompleted, handleMarqueeStart } = useNoteGridMarquee({
    notesRef, ppbRef, selectedRef, gridRef, setSelectedNoteIds,
  });
  const dragMemberMap = useMemo(
    () => (dragState ? new Map(dragState.members.map((m) => [m.noteId, m])) : null),
    [dragState]
  );

  const rects = useMemo(() => {
    return notes.map((n) => {
      const dm = dragMemberMap?.get(n.noteId);
      const isDrag = dm && dragState;
      const isResize = resizeState?.noteId === n.noteId;
      const beat = isDrag ? dm.startBeat + dragState.beatDelta : n.startBeat;
      const pitch = isDrag ? dm.startPitch + dragState.pitchDelta : n.pitch;
      const dur = isResize ? resizeState.currentDuration : n.durationBeats;
      return {
        x: beat * pixelsPerBeat,
        y: (127 - pitch) * KEY_HEIGHT,
        w: Math.max(2, dur * pixelsPerBeat),
        h: KEY_HEIGHT - 1,
        noteId: n.noteId,
        vel: n.velocity,
      };
    });
  }, [notes, dragState, resizeState, pixelsPerBeat, dragMemberMap]);
  const { handleMouseMove, handleMouseUp } = useNoteGridDrag({
    dragRef, resizeRef, ppbRef,
    setDragState, setResizeState, rpc, clipId,
    autoScroll, dragState, resizeState,
  });
  const {
    handleDoubleClick,
    deleteSelected,
    transposeSelected,
    quantizeSelected,
    humanizeSelected,
    handleKeyDown,
  } = useNoteGridInteractions({
    notes, noteMap, rpc, clipId, chordShape, quantizeStrength, swing,
    selectedNoteIds, setSelectedNoteIds, setContextMenu, gridRef, ppbRef,
  });
  const handleNoteMouseDown = useNoteGridNoteMouseDown({
    clipId, selectedNoteIds, setSelectedNoteIds, lastClickedNoteRef,
    noteMap, notes, setDragState, setResizeState,
  });

  const handleWheel = useCallback((e: React.WheelEvent) => {
    if (!e.ctrlKey || !onZoom) return;
    e.preventDefault();
    const el = gridRef.current;
    if (!el) return;
    const oldPpb = ppbRef.current;
    const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
    const newPpb = Math.round(Math.min(400, Math.max(20, oldPpb * factor)));
    if (newPpb !== oldPpb) onZoom(newPpb, el.getBoundingClientRect().left);
  }, [onZoom]);
  useEffect(() => {
    const el = gridRef.current;
    if (!el) return;
    let lastTime = 0;
    const handler = (e: WheelEvent) => {
      if (e.ctrlKey || e.metaKey) return;
      const now = performance.now();
      if (now - lastTime < 16) return;
      lastTime = now;
      e.preventDefault();
      el.scrollTop += e.deltaY > 0 ? 24 : -24;
    };
    el.addEventListener("wheel", handler, { passive: false });
    return () => el.removeEventListener("wheel", handler);
  }, []);

  const handleGridClick = useCallback(
    (e: React.MouseEvent) => {
      if (marqueeJustCompleted.current) { marqueeJustCompleted.current = false; return; }
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
      if (noteId != null && !selectedNoteIds.has(noteId)) setSelectedNoteIds(new Set([noteId]));
      setContextMenu({ x: e.clientX, y: e.clientY, noteId });
    },
    [selectedNoteIds, setSelectedNoteIds]
  );

  const handleScroll = useCallback(() => {
    const el = gridRef.current;
    if (el) { onVerticalScroll?.(el.scrollTop); onHorizontalScroll?.(el.scrollLeft); }
  }, [onVerticalScroll, onHorizontalScroll]);

  const contextActions = useMemo(() => {
    if (!contextMenu) return [];
    return [
      { label: "Quantize", shortcut: "Q", action: quantizeSelected },
      { label: "Humanize", shortcut: "H", action: humanizeSelected },
      { label: "Transpose Up +1", shortcut: "\u2191", action: () => transposeSelected(1) },
      { label: "Transpose Down -1", shortcut: "\u2193", action: () => transposeSelected(-1) },
      { label: "Transpose Up Octave", shortcut: "Ctrl+\u2191", action: () => transposeSelected(12) },
      { label: "Transpose Down Octave", shortcut: "Ctrl+\u2193", action: () => transposeSelected(-12) },
      { label: "Delete Selected", shortcut: "Del", action: deleteSelected },
    ];
  }, [contextMenu, quantizeSelected, humanizeSelected, transposeSelected, deleteSelected]);

  return (
    <div
      className="note-grid"
      ref={gridRef}
      style={{ "--ng-px-per-beat": `${pixelsPerBeat}px` } as React.CSSProperties}
      onDoubleClick={handleDoubleClick}
      onClick={handleGridClick}
      onMouseDown={handleMarqueeStart}
      onContextMenu={handleContextMenu}
      onScroll={handleScroll}
      onWheel={handleWheel}
      tabIndex={0}
      onKeyDown={handleKeyDown}
    >
      <div style={{ height: TOTAL_KEY_AREA, pointerEvents: "none" }} />
      {rects.length === 0 && (
        <div className="ng-empty">No notes</div>
      )}
      {rects.map((r) => {
        const isDragging = dragMemberMap?.has(r.noteId) ?? false;
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
            onMouseDown={(e) => handleNoteMouseDown(e, note, isDragging, isResizing)}
          />
        );
      })}

      {marquee && (
        <div
          className="ng-marquee"
          style={{
            left: Math.min(marquee.x1, marquee.x2),
            top: Math.min(marquee.y1, marquee.y2),
            width: Math.abs(marquee.x2 - marquee.x1),
            height: Math.abs(marquee.y2 - marquee.y1),
          }}
        />
      )}

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
