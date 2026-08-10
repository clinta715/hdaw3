import { useState, useRef, useCallback } from "react";
import { useTransportStore } from "../../store/transportStore";
import { rpc } from "../../rpc";

interface UseTimelineRulerOpts {
  pps: number;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

export function useTimelineRuler(opts: UseTimelineRulerOpts) {
  const { pps, tracksRef } = opts;
  const [isScrubbing, setIsScrubbing] = useState(false);
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
    [pps, tracksRef, beatToSec]
  );

  return { isScrubbing, handleRulerMouseDown };
}
