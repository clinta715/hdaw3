import { useMemo, useState, useCallback, useRef, useEffect } from "react";
import { NoteSnapshot } from "../rpc/types";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { snap } from "./snapUtils";
import { quantizeWithGroove } from "./grooveUtils";
import "./NoteGrid.css";
import { useAutoScroll } from "../hooks/useAutoScroll";

interface Props {
  notes: NoteSnapshot[];
  rpc: RpcClient;
  clipId: number | null;
  pixelsPerBeat: number;
  onVerticalScroll?: (scrollTop: number) => void;
  onHorizontalScroll?: (scrollLeft: number) => void;
  onZoom?: (newPixelsPerBeat: number, anchorClientX: number) => void;
  selectedNoteIds?: Set<number>;
  onSelectionChange?: (ids: Set<number>) => void;
  chordShape?: number[];
  quantizeStrength?: number;
  swing?: number;
}

interface NoteDragState {
  members: Array<{ noteId: number; startPitch: number; startBeat: number }>;
  anchorIndex: number;
  offsetX: number;
  offsetY: number;
  minPitch: number;
  maxPitch: number;
  minBeat: number;
  pitchDelta: number;
  beatDelta: number;
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

interface MarqueeState {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  additive: boolean;
  baseSelection: Set<number>;
}

const KEY_HEIGHT = 8;
const TOTAL_KEY_AREA = 128 * KEY_HEIGHT;
const DRAG_THRESHOLD = 4;

function clamp(val: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, val));
}

let noteClipboard: NoteSnapshot[] = [];

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

  const [marquee, setMarquee] = useState<MarqueeState | null>(null);
  const marqueeJustCompleted = useRef(false);

  const gridRef = useRef<HTMLDivElement>(null);
  const autoScroll = useAutoScroll(gridRef);
  const lastClickedNoteRef = useRef<number | null>(null);
  const ppbRef = useRef(pixelsPerBeat);
  ppbRef.current = pixelsPerBeat;

  const notesRef = useRef(notes);
  notesRef.current = notes;
  const selectedRef = useRef(selectedNoteIds);
  selectedRef.current = selectedNoteIds;

  const noteMap = useMemo(() => {
    const m = new Map<number, NoteSnapshot>();
    for (const n of notes) m.set(n.noteId, n);
    return m;
  }, [notes]);

  // Intersection test must match note rendering geometry exactly. Reads via refs
  // so the window-level marquee handlers never touch stale closure props.
  const intersectNotes = useCallback(
    (mx0: number, my0: number, mx1: number, my1: number): number[] => {
      const hits: number[] = [];
      for (const n of notesRef.current) {
        const nx = n.startBeat * ppbRef.current;
        const ny = (127 - n.pitch) * KEY_HEIGHT;
        const nw = Math.max(2, n.durationBeats * ppbRef.current);
        const nh = KEY_HEIGHT - 1;
        if (nx < mx1 && nx + nw > mx0 && ny < my1 && ny + nh > my0) hits.push(n.noteId);
      }
      return hits;
    },
    []
  );

  const dragMemberMap = useMemo(
    () => (dragState ? new Map(dragState.members.map((m) => [m.noteId, m])) : null),
    [dragState]
  );

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

      const dragMember = dragMemberMap?.get(n.noteId);
      if (dragMember && dragState) {
        x = (dragMember.startBeat + dragState.beatDelta) * pixelsPerBeat;
        y = (127 - (dragMember.startPitch + dragState.pitchDelta)) * KEY_HEIGHT;
        w = Math.max(2, n.durationBeats * pixelsPerBeat);
      } else if (resizeState && resizeState.noteId === n.noteId) {
        x = n.startBeat * pixelsPerBeat;
        y = (127 - n.pitch) * KEY_HEIGHT;
        w = Math.max(2, resizeState.currentDuration * pixelsPerBeat);
      } else {
        x = n.startBeat * pixelsPerBeat;
        y = (127 - n.pitch) * KEY_HEIGHT;
        w = Math.max(2, n.durationBeats * pixelsPerBeat);
      }

      const h = KEY_HEIGHT - 1;
      return { x, y, w, h, noteId: n.noteId, vel: n.velocity };
    });
  }, [notes, dragState, resizeState, pixelsPerBeat, dragMemberMap]);

  const handleMouseMove = useCallback((e: globalThis.MouseEvent) => {
    autoScroll.update(e.clientX, e.clientY);
    const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } = useUiStore.getState();
    const settings = { enabled: snapEnabled, division: snapDivision, gridOffset: snapGridOffset, events: snapToEvents };

    setResizeState((prev) => {
      if (!prev) return null;
      const deltaX = e.clientX - prev.startX;
      const rawDuration = Math.max(0.03125, prev.initialDuration + deltaX / ppbRef.current);
      const newDuration = Math.max(0.03125, snap(rawDuration, settings));
      return { ...prev, currentDuration: newDuration };
    });

    setDragState((prev) => {
      if (!prev) return null;
      const pitchDelta = clamp(
        -Math.round((e.clientY - prev.offsetY) / KEY_HEIGHT),
        -prev.minPitch,
        127 - prev.maxPitch
      );
      const anchor = prev.members[prev.anchorIndex];
      const rawStart = Math.max(0, anchor.startBeat + (e.clientX - prev.offsetX) / ppbRef.current);
      const snappedAnchorStart = snap(rawStart, settings, { originalStart: anchor.startBeat });
      let beatDelta = snappedAnchorStart - anchor.startBeat;
      if (prev.minBeat + beatDelta < 0) beatDelta = -prev.minBeat;
      return { ...prev, pitchDelta, beatDelta };
    });
  }, []);

  const handleMouseUp = useCallback(async () => {
    autoScroll.stop();
    const drag = dragRef.current;
    const resize = resizeRef.current;

    // Optimistic local update: write the committed pitch/start/duration into
    // the notesByClip store BEFORE clearing the drag/resize preview, so the
    // notes don't snap back to their pre-gesture positions for the round-trip
    // until syncNotes returns.
    if ((drag || resize) && clipId != null) {
      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId);
        if (!arr) return {};
        const memberMap = drag ? new Map(drag.members.map((m) => [m.noteId, m])) : null;
        return {
          notesByClip: new Map(s.notesByClip).set(
            clipId,
            arr.map((n) => {
              const member = memberMap?.get(n.noteId);
              if (member) {
                return {
                  ...n,
                  pitch: clamp(member.startPitch + drag!.pitchDelta, 0, 127),
                  startBeat: Math.max(0, member.startBeat + drag!.beatDelta),
                };
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
        if (drag.pitchDelta !== 0 || drag.beatDelta !== 0) {
          if (drag.members.length > 1) {
            await rpc.call("project.beginTransaction", { name: "move notes" });
          }
          for (const m of drag.members) {
            await rpc.call("project.setNotePitch", { noteId: m.noteId, pitch: clamp(m.startPitch + drag.pitchDelta, 0, 127) });
            await rpc.call("project.setNoteStart", { noteId: m.noteId, startBeat: Math.max(0, m.startBeat + drag.beatDelta) });
          }
          if (drag.members.length > 1) {
            await rpc.call("project.endTransaction");
          }
        }
      }
      if (resize && clipId != null) {
        const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } = useUiStore.getState();
        const snappedDuration = Math.max(0.03125, snap(resize.currentDuration, { enabled: snapEnabled, division: snapDivision, gridOffset: snapGridOffset, events: snapToEvents }));
        await rpc.call("project.setNoteDuration", { noteId: resize.noteId, durationBeats: snappedDuration });
      }
      if ((drag || resize) && clipId != null) {
        useProjectStore.getState().syncNotes(rpc, clipId);
      }
    } catch (err) {
      console.warn("note interaction failed", err);
    }
    useUiStore.getState().setStatusHint(null);
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

  // Marquee (rubber-band) selection on empty grid mousedown. Window-level
  // listeners are added at mousedown and removed at mouseup (self-managed, like
  // useTimelineRubberBand) — the grid's own onMouseMove/Up/Leave handlers are
  // for note drag/resize only.
  const handleMarqueeStart = useCallback(
    (e: React.MouseEvent) => {
      if (e.button !== 0) return;
      // Prevent native text selection during the marquee drag. preventDefault
      // also suppresses focus, so restore it for the grid's onKeyDown shortcuts.
      e.preventDefault();
      gridRef.current?.focus();
      marqueeJustCompleted.current = false;
      const el = gridRef.current;
      if (!el) return;
      const rect = el.getBoundingClientRect();
      const x1 = e.clientX - rect.left + el.scrollLeft;
      const y1 = e.clientY - rect.top + el.scrollTop;
      const startClientX = e.clientX;
      const startClientY = e.clientY;
      const additive = e.shiftKey;
      const baseSelection = new Set(selectedRef.current);
      let activated = false;

      const onMove = (ev: globalThis.MouseEvent) => {
        if (!activated) {
          const dx = ev.clientX - startClientX;
          const dy = ev.clientY - startClientY;
          if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) return;
          activated = true;
          setMarquee({ x1, y1, x2: x1, y2: y1, additive, baseSelection });
        }
        const r = el.getBoundingClientRect();
        const x2 = ev.clientX - r.left + el.scrollLeft;
        const y2 = ev.clientY - r.top + el.scrollTop;
        setMarquee((prev) => (prev ? { ...prev, x2, y2 } : prev));
        const mx0 = Math.min(x1, x2);
        const my0 = Math.min(y1, y2);
        const mx1 = Math.max(x1, x2);
        const my1 = Math.max(y1, y2);
        const ids = additive ? new Set(baseSelection) : new Set<number>();
        for (const id of intersectNotes(mx0, my0, mx1, my1)) ids.add(id);
        setSelectedNoteIds(ids);
      };

      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        if (activated) {
          marqueeJustCompleted.current = true;
          setMarquee(null);
        }
      };

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [setSelectedNoteIds, intersectNotes]
  );

  const handleDoubleClick = useCallback(async (e: React.MouseEvent) => {
    if (clipId == null) return;
    if ((e.target as HTMLElement).closest(".ng-note")) return;

    const gridEl = gridRef.current;
    if (!gridEl) return;
    const rect = gridEl.getBoundingClientRect();
    const x = e.clientX - rect.left + gridEl.scrollLeft;
    const y = e.clientY - rect.top + gridEl.scrollTop;
    const pitch = clamp(127 - Math.floor(y / KEY_HEIGHT), 0, 127);
    const rawBeat = x / ppbRef.current;
    const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } = useUiStore.getState();
    const startBeat = snap(rawBeat, { enabled: snapEnabled, division: snapDivision, gridOffset: snapGridOffset, events: snapToEvents });
    
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
    
    const strength = quantizeStrength / 100;

    // Optimistic: update start times in local store immediately
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.map((n) => {
            if (!selectedNoteIds.has(n.noteId)) return n;
            const newStart = quantizeWithGroove(n.startBeat, snapDivision, strength, swing);
            return { ...n, startBeat: newStart };
          })
        ),
      };
    });
    
    try {
      for (const noteId of selectedNoteIds) {
        const note = noteMap.get(noteId);
        if (!note) continue;
        const newStart = quantizeWithGroove(note.startBeat, snapDivision, strength, swing);
        await rpc.call("project.setNoteStart", { noteId, startBeat: newStart });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("quantize failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, noteMap, quantizeStrength, swing]);

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
    const scrollBeat = gridEl ? gridEl.scrollLeft / ppbRef.current : 0;
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

  // Ctrl+wheel zoom: scale pixelsPerBeat, keep the beat under the cursor fixed.
  const handleWheel = useCallback(
    (e: React.WheelEvent) => {
      if (!e.ctrlKey || !onZoom) return;
      e.preventDefault();
      const el = gridRef.current;
      if (!el) return;
      const oldPpb = ppbRef.current;
      const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
      const newPpb = Math.round(Math.min(400, Math.max(20, oldPpb * factor)));
      if (newPpb === oldPpb) return;
      const rect = el.getBoundingClientRect();
      const anchorX = e.clientX - rect.left;
      onZoom(newPpb, e.clientX - anchorX);
    },
    [onZoom]
  );

  // Throttled plain-wheel scroll: reduce default browser scroll speed.
  // React's onWheel is passive in modern browsers, so we use a native
  // addEventListener with { passive: false } to get preventDefault().
  useEffect(() => {
    const el = gridRef.current;
    if (!el) return;
    const SCROLL_LINES = 3; // ~3 key rows per wheel notch
    const PIXELS_PER_LINE = KEY_HEIGHT; // 8px per key row
    let lastTime = 0;
    const handler = (e: WheelEvent) => {
      if (e.ctrlKey || e.metaKey) return; // let Ctrl+wheel zoom pass through
      const now = performance.now();
      if (now - lastTime < 16) return; // ~60fps throttle
      lastTime = now;
      e.preventDefault();
      const delta = e.deltaY > 0 ? SCROLL_LINES * PIXELS_PER_LINE : -SCROLL_LINES * PIXELS_PER_LINE;
      el.scrollTop += delta;
    };
    el.addEventListener("wheel", handler, { passive: false });
    return () => el.removeEventListener("wheel", handler);
  }, []);

  const handleGridClick = useCallback(
    (e: React.MouseEvent) => {
      // A marquee just finished: the click event that follows a real drag must
      // not wipe the freshly selected notes.
      if (marqueeJustCompleted.current) {
        marqueeJustCompleted.current = false;
        return;
      }
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

              if (e.shiftKey) {
                const lastNote = lastClickedNoteRef.current != null ? noteMap.get(lastClickedNoteRef.current) : null;
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
                // Shift + different pitch (or no last-clicked): add to selection.
                setSelectedNoteIds((prev) => new Set(prev).add(note.noteId));
                lastClickedNoteRef.current = note.noteId;
                return;
              }

              if (!selectedNoteIds.has(note.noteId)) {
                setSelectedNoteIds(new Set([note.noteId]));
              }
              lastClickedNoteRef.current = note.noteId;

              const noteRect = (e.currentTarget as HTMLElement).getBoundingClientRect();
              const localX = e.clientX - noteRect.left;
              useUiStore.getState().setStatusHint("Draw: click · Drag: move/resize note · Alt+drag: quick-draw row");
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
                    return n ? { noteId: id, startPitch: n.pitch, startBeat: n.startBeat } : null;
                  })
                  .filter((m): m is { noteId: number; startPitch: number; startBeat: number } => m != null);
                const anchorIndex = Math.max(0, members.findIndex((m) => m.noteId === note.noteId));
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
            }}
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
