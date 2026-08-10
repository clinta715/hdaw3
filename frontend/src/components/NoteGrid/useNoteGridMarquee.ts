import { useState, useCallback, useRef } from "react";
import { MarqueeState } from "./noteGridTypes";
import { KEY_HEIGHT, DRAG_THRESHOLD } from "./noteGridConstants";

interface UseNoteGridMarqueeOptions {
  notesRef: React.RefObject<import("../../rpc/types").NoteSnapshot[]>;
  ppbRef: React.RefObject<number>;
  selectedRef: React.RefObject<Set<number>>;
  gridRef: React.RefObject<HTMLDivElement | null>;
  setSelectedNoteIds: (ids: Set<number> | ((prev: Set<number>) => Set<number>)) => void;
}

export function useNoteGridMarquee({
  notesRef,
  ppbRef,
  selectedRef,
  gridRef,
  setSelectedNoteIds,
}: UseNoteGridMarqueeOptions) {
  const [marquee, setMarquee] = useState<MarqueeState | null>(null);
  const marqueeJustCompleted = useRef(false);

  const intersectNotes = useCallback(
    (mx0: number, my0: number, mx1: number, my1: number): number[] => {
      const hits: number[] = [];
      for (const n of notesRef.current) {
        const nx = n.startBeat * ppbRef.current;
        const ny = (127 - n.pitch) * KEY_HEIGHT;
        const nw = Math.max(2, n.durationBeats * ppbRef.current);
        const nh = KEY_HEIGHT - 1;
        if (nx < mx1 && nx + nw > mx0 && ny < my1 && ny + nh > my0)
          hits.push(n.noteId);
      }
      return hits;
    },
    [notesRef, ppbRef]
  );

  const handleMarqueeStart = useCallback(
    (e: React.MouseEvent) => {
      if (e.button !== 0) return;
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
    [setSelectedNoteIds, intersectNotes, gridRef, selectedRef]
  );

  return { marquee, marqueeJustCompleted, handleMarqueeStart };
}
