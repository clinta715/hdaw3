import { useState, useRef, useCallback } from "react";
import type { ClipSnapshot } from "../rpc/types";
import type { RowLayout } from "../utils/rowLayout";
import { useUiStore } from "../store/uiStore";
import { computeRubberBandSelection } from "../utils/timelineConstants";
import { MIN_PPS, MAX_PPS } from "./useTimelineZoom";

const DRAG_THRESHOLD = 4;

interface UseTimelineRubberBandParams {
  clips: ClipSnapshot[];
  pps: number;
  layout: RowLayout;
  selectedClipIds: Set<number>;
  engagementRef: React.MutableRefObject<"none" | "clip" | "rubber" | "zoom">;
}

interface UseTimelineRubberBandReturn {
  handleRubberBandStart: (e: React.MouseEvent) => void;
  rubberBand: { x1: number; y1: number; x2: number; y2: number } | null;
  rubberBandJustCompleted: React.MutableRefObject<boolean>;
}

export function useTimelineRubberBand({
  clips,
  pps,
  layout,
  selectedClipIds,
  tracksRef,
  engagementRef,
  setPps,
}: UseTimelineRubberBandParams & {
  tracksRef: React.RefObject<HTMLDivElement | null>;
  setPps: React.Dispatch<React.SetStateAction<number>>;
}): UseTimelineRubberBandReturn {
  const [rubberBand, setRubberBand] = useState<{ x1: number; y1: number; x2: number; y2: number } | null>(null);
  const rubberBandRef = useRef(rubberBand);
  rubberBandRef.current = rubberBand;
  const rubberBandJustCompleted = useRef(false);
  const zoomStartRef = useRef({ pps: 0, y: 0 });

  const handleRubberBandStart = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return;
    // Prevent the browser from initiating native text selection during the
    // drag — otherwise dragging across clips highlights their name labels
    // instead of (or in addition to) the rubber band selecting them.
    e.preventDefault();
    rubberBandJustCompleted.current = false;
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const x1 = e.clientX - rect.left + el.scrollLeft;
    const y1 = e.clientY - rect.top + el.scrollTop;
    const startClientX = e.clientX;
    const startClientY = e.clientY;
    let activated = false;

    const onMove = (ev: globalThis.MouseEvent) => {
      if (!activated) {
        if (engagementRef.current === "clip") {
          window.removeEventListener("mousemove", onMove);
          window.removeEventListener("mouseup", onUp);
          return;
        }
        const dx = ev.clientX - startClientX;
        const dy = ev.clientY - startClientY;
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) return;
        // Predominantly vertical drag → zoom mode
        if (Math.abs(dy) > Math.abs(dx) * 2) {
          activated = true;
          engagementRef.current = "zoom";
          zoomStartRef.current = { pps, y: startClientY };
          return;
        }
        activated = true;
        engagementRef.current = "rubber";
        setRubberBand({ x1, y1, x2: x1, y2: y1 });
      }
      if (engagementRef.current === "zoom") {
        const dy = ev.clientY - zoomStartRef.current.y;
        const factor = Math.pow(1.005, -dy);
        setPps(Math.min(MAX_PPS, Math.max(MIN_PPS, zoomStartRef.current.pps * factor)));
        return;
      }
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
          rb.x1, rb.y1, newX2, newY2, clips, pps, layout) });
      }
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const wasZoom = engagementRef.current === "zoom";
      engagementRef.current = "none";
      if (activated && !wasZoom) {
        const rb = rubberBandRef.current;
        if (rb) {
          const selected = computeRubberBandSelection(rb.x1, rb.y1, rb.x2, rb.y2, clips, pps, layout);
          useUiStore.setState({ selectedClipIds: selected });
          rubberBandJustCompleted.current = true;
        }
        setRubberBand(null);
      }
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [clips, pps, tracksRef, engagementRef, setPps]);

  return { handleRubberBandStart, rubberBand, rubberBandJustCompleted };
}
