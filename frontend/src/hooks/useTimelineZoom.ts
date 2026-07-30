import { useState, useCallback, useEffect, useRef } from "react";

export const DEFAULT_PPS = 40;
export const MIN_PPS = 10;
export const MAX_PPS = 200;

interface UseTimelineZoomParams {
  maxEnd: number;
  bodyRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineZoomReturn {
  pps: number;
  setPps: React.Dispatch<React.SetStateAction<number>>;
  zoomIn: () => void;
  zoomOut: () => void;
  zoomFit: () => void;
}

export function useTimelineZoom({ maxEnd, bodyRef }: UseTimelineZoomParams): UseTimelineZoomReturn {
  const [pps, setPps] = useState(DEFAULT_PPS);

  const zoomIn = useCallback(() => setPps((p) => Math.min(MAX_PPS, p * 1.25)), []);
  const zoomOut = useCallback(() => setPps((p) => Math.max(MIN_PPS, p / 1.25)), []);
  const zoomFit = useCallback(() => {
    if (maxEnd <= 0) { setPps(DEFAULT_PPS); return; }
    const cw = bodyRef.current?.clientWidth ?? 800;
    setPps(Math.round(Math.min(MAX_PPS, Math.max(MIN_PPS, cw / maxEnd))));
  }, [maxEnd, bodyRef]);

  // Native wheel handler with { passive: false } so preventDefault() actually
  // stops the browser's native Ctrl+wheel page zoom. React's onWheel is
  // passive in modern browsers — preventDefault() is silently ignored.
  useEffect(() => {
    const el = bodyRef.current;
    if (!el) return;
    const handler = (e: WheelEvent) => {
      if (!e.ctrlKey && !e.metaKey) return;
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.25 : 0.8;
      setPps((p) => Math.min(MAX_PPS, Math.max(MIN_PPS, p * factor)));
    };
    el.addEventListener("wheel", handler, { passive: false });
    return () => el.removeEventListener("wheel", handler);
  }, [bodyRef]);

  return { pps, setPps, zoomIn, zoomOut, zoomFit };
}
