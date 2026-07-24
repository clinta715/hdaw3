import { useState, useRef, useCallback } from "react";
import type { ClipSnapshot } from "../rpc/types";
import { useUiStore } from "../store/uiStore";
import { computeRubberBandSelection } from "../utils/timelineConstants";

interface UseTimelineRubberBandParams {
  clips: ClipSnapshot[];
  pps: number;
  TRACK_HEIGHT: number;
  selectedClipIds: Set<number>;
}

interface UseTimelineRubberBandReturn {
  handleRubberBandStart: (e: React.MouseEvent) => void;
  rubberBand: { x1: number; y1: number; x2: number; y2: number } | null;
  rubberBandJustCompleted: React.MutableRefObject<boolean>;
}

export function useTimelineRubberBand({
  clips,
  pps,
  TRACK_HEIGHT,
  selectedClipIds,
  tracksRef,
}: UseTimelineRubberBandParams & {
  tracksRef: React.RefObject<HTMLDivElement | null>;
}): UseTimelineRubberBandReturn {
  const [rubberBand, setRubberBand] = useState<{ x1: number; y1: number; x2: number; y2: number } | null>(null);
  const rubberBandRef = useRef(rubberBand);
  rubberBandRef.current = rubberBand;
  const rubberBandJustCompleted = useRef(false);

  const handleRubberBandStart = useCallback((e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest(".tl-clip")) return;
    rubberBandJustCompleted.current = false;
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left + el.scrollLeft;
    const y = e.clientY - rect.top + el.scrollTop;
    setRubberBand({ x1: x, y1: y, x2: x, y2: y });

    const onMove = (ev: globalThis.MouseEvent) => {
      const r = el.getBoundingClientRect();
      const newX2 = ev.clientX - r.left + el.scrollLeft;
      const newY2 = ev.clientY - r.top + el.scrollTop;
      setRubberBand(prev => prev ? {
        ...prev,
        x2: newX2,
        y2: newY2,
      } : null);
      const rb = rubberBandRef.current;
      if (rb) {
        useUiStore.setState({ selectedClipIds: computeRubberBandSelection(
          rb.x1, rb.y1, newX2, newY2, clips, pps, TRACK_HEIGHT) });
      }
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const rb = rubberBandRef.current;
      if (rb) {
        const selected = computeRubberBandSelection(rb.x1, rb.y1, rb.x2, rb.y2, clips, pps, TRACK_HEIGHT);
        if (selected.size > 0) {
          useUiStore.setState({ selectedClipIds: selected });
          rubberBandJustCompleted.current = true;
        }
      }
      setRubberBand(null);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [clips, pps, tracksRef]);

  return { handleRubberBandStart, rubberBand, rubberBandJustCompleted };
}
