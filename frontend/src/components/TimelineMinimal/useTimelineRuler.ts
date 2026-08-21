import { useState, useRef, useCallback } from "react";
import { useTransportStore } from "../../store/transportStore";
import { useMarkerStore } from "../../store/markerStore";
import { useUiStore } from "../../store/uiStore";
import { snap } from "../snapUtils";
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

      // Marker pin drag wins over seek/scrub/marquee when the gesture starts
      // on a pin. A release without movement falls through to the pin's
      // click-to-seek; Escape cancels and restores the original position.
      const pin = (e.target as HTMLElement).closest?.(".tl-marker-pin") as HTMLElement | null;
      if (pin && e.button === 0) {
        const pins = Array.from(pin.parentElement?.querySelectorAll(".tl-marker-pin") ?? []);
        const grabbed = useMarkerStore.getState().markers[pins.indexOf(pin)];
        if (!grabbed) return;
        e.preventDefault();
        const markerIndex = grabbed.index;
        const originalTime = grabbed.time;
        let moved = false;
        let cancelled = false;

        const applyTime = (time: number) => {
          useMarkerStore.setState({
            markers: useMarkerStore.getState().markers.map((m) =>
              m.index === markerIndex ? { ...m, time } : m
            ),
          });
        };

        const onMove = (ev: globalThis.MouseEvent) => {
          if (cancelled) return;
          const r = tracksRef.current?.getBoundingClientRect();
          if (!r) return;
          const s = tracksRef.current?.scrollLeft ?? 0;
          const b = Math.max(0, (ev.clientX - r.left + s) / pps);
          const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } = useUiStore.getState();
          const snapped = Math.max(
            0,
            snap(
              b,
              { enabled: snapEnabled, division: snapDivision, gridOffset: snapGridOffset, events: snapToEvents },
              { originalStart: originalTime }
            )
          );
          if (snapped !== originalTime) moved = true;
          applyTime(snapped);
        };

        const onUp = () => {
          window.removeEventListener("mousemove", onMove);
          window.removeEventListener("mouseup", onUp);
          window.removeEventListener("keydown", onKey);
          if (cancelled || !moved) {
            window.removeEventListener("click", swallowClick, true);
            return;
          }
          const current = useMarkerStore.getState().markers.find((m) => m.index === markerIndex);
          if (current && current.time !== originalTime) {
            rpc
              .call("project.setMarkerTime", { index: markerIndex, time: current.time })
              .then(() => useMarkerStore.getState().syncMarkers(rpc))
              .catch(() => {});
            window.setTimeout(() => window.removeEventListener("click", swallowClick, true), 0);
          } else {
            window.removeEventListener("click", swallowClick, true);
          }
        };

        const onKey = (ev: globalThis.KeyboardEvent) => {
          if (ev.key !== "Escape") return;
          cancelled = true;
          applyTime(originalTime);
          window.removeEventListener("mousemove", onMove);
          window.removeEventListener("mouseup", onUp);
          window.removeEventListener("keydown", onKey);
        };

        // A moved drag would otherwise fire the pin's click-to-seek on
        // release; swallow that one click in the capture phase.
        const swallowClick = (ev: globalThis.MouseEvent) => {
          window.removeEventListener("click", swallowClick, true);
          if (moved || cancelled) {
            ev.stopPropagation();
            ev.preventDefault();
          }
        };

        window.addEventListener("mousemove", onMove);
        window.addEventListener("mouseup", onUp);
        window.addEventListener("keydown", onKey);
        window.addEventListener("click", swallowClick, true);
        return;
      }

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
