import { useState, useCallback } from "react";

export const DEFAULT_PPS = 40;
export const MIN_PPS = 10;
export const MAX_PPS = 200;

interface UseTimelineZoomParams {
  maxEnd: number;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineZoomReturn {
  pps: number;
  setPps: React.Dispatch<React.SetStateAction<number>>;
  zoomIn: () => void;
  zoomOut: () => void;
  zoomFit: () => void;
  onWheel: (e: React.WheelEvent) => void;
}

export function useTimelineZoom({ maxEnd, tracksRef }: UseTimelineZoomParams): UseTimelineZoomReturn {
  const [pps, setPps] = useState(DEFAULT_PPS);

  const zoomIn = useCallback(() => setPps((p) => Math.min(MAX_PPS, p * 1.25)), []);
  const zoomOut = useCallback(() => setPps((p) => Math.max(MIN_PPS, p / 1.25)), []);
  const zoomFit = useCallback(() => {
    if (maxEnd <= 0) { setPps(DEFAULT_PPS); return; }
    const cw = tracksRef.current?.clientWidth ?? 800;
    setPps(Math.round(Math.min(MAX_PPS, Math.max(MIN_PPS, cw / maxEnd))));
  }, [maxEnd, tracksRef]);

  const onWheel = useCallback((e: React.WheelEvent) => {
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault();
    setPps((p) => {
      const factor = e.deltaY < 0 ? 1.25 : 0.8;
      return Math.min(MAX_PPS, Math.max(MIN_PPS, p * factor));
    });
  }, []);

  return { pps, setPps, zoomIn, zoomOut, zoomFit, onWheel };
}
