import { useState, useCallback, useEffect, useLayoutEffect, useRef } from "react";

export const DEFAULT_PPS = 40;
export const MIN_PPS = 10;
export const MAX_PPS = 200;

interface UseTimelineZoomParams {
  maxEnd: number;
  bodyRef: React.RefObject<HTMLDivElement | null>;
  tracksRef: React.RefObject<HTMLDivElement | null>;
  rulerRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineZoomReturn {
  pps: number;
  setPps: React.Dispatch<React.SetStateAction<number>>;
  zoomIn: () => void;
  zoomOut: () => void;
  zoomFit: () => void;
  zoomToRange: (b1: number, b2: number, viewportWidth: number) => void;
}

const clampPps = (v: number) => Math.min(MAX_PPS, Math.max(MIN_PPS, v));

export function useTimelineZoom({ maxEnd, bodyRef, tracksRef, rulerRef }: UseTimelineZoomParams): UseTimelineZoomReturn {
  const [pps, setPps] = useState(DEFAULT_PPS);
  // ppsRef avoids a stale-closure in the wheel listener (attached once per
  // effect run). Reassigned every render so the handler always reads current.
  const ppsRef = useRef(pps);
  ppsRef.current = pps;
  // Scroll target to restore after a pps change. The new totalW commits in a
  // layout effect before paint, so applying scroll here keeps the beat under
  // the cursor pinned without a visible flicker.
  const pendingScrollRef = useRef<number | null>(null);

  const zoomAt = useCallback((clientX: number, factor: number) => {
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const cursorOffset = clientX - rect.left;
    const beatAtCursor = (cursorOffset + el.scrollLeft) / ppsRef.current;
    const next = clampPps(ppsRef.current * factor);
    pendingScrollRef.current = Math.max(0, beatAtCursor * next - cursorOffset);
    setPps(next);
  }, [tracksRef]);

  const zoomToRange = useCallback((b1: number, b2: number, viewportWidth: number) => {
    const lo = Math.min(b1, b2);
    const hi = Math.max(b1, b2);
    if (hi <= lo || viewportWidth <= 0) return;
    const next = clampPps(viewportWidth / (hi - lo));
    pendingScrollRef.current = Math.max(0, lo * next);
    setPps(next);
  }, []);

  // Apply the pending scroll after React commits the new pps (and thus the
  // new totalW). Native onScroll only fires from user input, so we mirror the
  // ruler+arranger sync here too.
  useLayoutEffect(() => {
    if (pendingScrollRef.current == null) return;
    const tracks = tracksRef.current;
    if (tracks) {
      tracks.scrollLeft = pendingScrollRef.current;
      if (rulerRef.current) rulerRef.current.scrollLeft = tracks.scrollLeft;
    }
    pendingScrollRef.current = null;
  }, [pps, tracksRef, rulerRef]);

  const zoomIn = useCallback(() => setPps((p) => clampPps(p * 1.25)), []);
  const zoomOut = useCallback(() => setPps((p) => clampPps(p / 1.25)), []);
  const zoomFit = useCallback(() => {
    if (maxEnd <= 0) { setPps(DEFAULT_PPS); return; }
    const cw = bodyRef.current?.clientWidth ?? 800;
    setPps(Math.round(clampPps(cw / maxEnd)));
  }, [maxEnd, bodyRef]);

  // Single listener on the timeline body. { passive: false } is required so
  // preventDefault() actually stops Chromium's Ctrl+wheel page zoom — React's
  // onWheel is passive in modern browsers and silently ignores it.
  useEffect(() => {
    const el = bodyRef.current;
    if (!el) return;
    const isInsideRuler = (t: EventTarget | null) => {
      const ruler = rulerRef.current;
      return !!(ruler && t instanceof Node && ruler.contains(t));
    };
    const handler = (e: WheelEvent) => {
      const isZoomGesture = e.ctrlKey || e.metaKey || isInsideRuler(e.target);
      const isShiftOnly = e.shiftKey && !e.ctrlKey && !e.metaKey && !e.altKey;
      if (isShiftOnly) return;              // native horizontal scroll
      if (!isZoomGesture) return;           // native vertical scroll on tracks
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.25 : 0.8;
      zoomAt(e.clientX, factor);
    };
    el.addEventListener("wheel", handler, { passive: false });
    return () => el.removeEventListener("wheel", handler);
  }, [bodyRef, rulerRef, tracksRef, zoomAt]);

  return { pps, setPps, zoomIn, zoomOut, zoomFit, zoomToRange };
}
