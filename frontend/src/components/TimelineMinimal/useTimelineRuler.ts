import { useState, useRef, useCallback } from "react";
import { useTransportStore } from "../../store/transportStore";
import { rpc } from "../../rpc";

interface UseTimelineRulerOpts {
  pps: number;
  tracksRef: React.RefObject<HTMLDivElement | null>;
  onMarqueeZoom: (b1: number, b2: number, viewportWidth: number) => void;
}

interface ZoomRect { x1: number; x2: number; }

export function useTimelineRuler(opts: UseTimelineRulerOpts) {
  const { pps, tracksRef, onMarqueeZoom } = opts;
  const [isScrubbing, setIsScrubbing] = useState(false);
  const [zoomRect, setZoomRect] = useState<ZoomRect | null>(null);
  const scrubRef = useRef(false);

  const beatToSec = useCallback(
    (beat: number) => beat * 60 / useTransportStore.getState().transport.bpm,
    []
  );

  const handleRulerMouseDown = useCallback(
    (e: React.MouseEvent) => {
      const rect = tracksRef.current?.getBoundingClientRect();
      if (!rect) return;
      const scroll = tracksRef.current?.scrollLeft ?? 0;
      const beat = Math.max(0, (e.clientX - rect.left + scroll) / pps);

      // Ctrl+Alt+drag = marquee zoom. Must precede the Ctrl+click / Alt+click
      // loop-set branches below so it wins that gesture collision.
      if (e.button === 0 && e.ctrlKey && e.altKey && !e.shiftKey) {
        e.preventDefault();
        const x1 = e.clientX - rect.left + scroll;
        setZoomRect({ x1, x2: x1 });

        const onMove = (ev: globalThis.MouseEvent) => {
          const r = tracksRef.current?.getBoundingClientRect();
          if (!r) return;
          const s = tracksRef.current?.scrollLeft ?? 0;
          setZoomRect({ x1, x2: ev.clientX - r.left + s });
        };
        const onUp = (ev: globalThis.MouseEvent) => {
          window.removeEventListener("mousemove", onMove);
          window.removeEventListener("mouseup", onUp);
          setZoomRect(null);
          const r = tracksRef.current?.getBoundingClientRect();
          if (!r) return;
          const s = tracksRef.current?.scrollLeft ?? 0;
          const xEnd = ev.clientX - r.left + s;
          const b1 = x1 / pps;
          const b2 = xEnd / pps;
          // Sub-threshold drag = click; cancel (no zoom).
          if (Math.abs(b2 - b1) * pps < 8) return;
          onMarqueeZoom(b1, b2, r.width);
        };
        window.addEventListener("mousemove", onMove);
        window.addEventListener("mouseup", onUp);
        return;
      }

      // Ctrl+click = set loop start, Alt+click = set loop end
      if (e.ctrlKey || e.metaKey) {
        rpc.call("project.setLoopStart", { beat }).catch(() => {});
        const t = useTransportStore.getState().transport;
        useTransportStore.getState().update({ ...t, loopStart: beat });
        if (!t.isLooping) rpc.call("transport.toggleLoop").catch(() => {});
        return;
      }
      if (e.altKey) {
        rpc.call("project.setLoopEnd", { beat }).catch(() => {});
        const t = useTransportStore.getState().transport;
        useTransportStore.getState().update({ ...t, loopEnd: beat });
        if (!t.isLooping) rpc.call("transport.toggleLoop").catch(() => {});
        return;
      }

      rpc
        .call("transport.seekToSeconds", { seconds: beatToSec(beat) })
        .catch(() => {});
      scrubRef.current = true;
      setIsScrubbing(true);

      const onMove = (ev: globalThis.MouseEvent) => {
        if (!scrubRef.current) return;
        const r = tracksRef.current?.getBoundingClientRect();
        if (!r) return;
        const s = tracksRef.current?.scrollLeft ?? 0;
        const b = Math.max(0, (ev.clientX - r.left + s) / pps);
        rpc
          .call("transport.seekToSeconds", { seconds: beatToSec(b) })
          .catch(() => {});
      };

      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        scrubRef.current = false;
        setIsScrubbing(false);
      };

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [pps, tracksRef, beatToSec, onMarqueeZoom]
  );

  return { isScrubbing, handleRulerMouseDown, zoomRect };
}
