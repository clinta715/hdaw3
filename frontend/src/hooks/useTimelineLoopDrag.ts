import { useState, useRef, useCallback } from "react";
import type { TransportSnapshot } from "../rpc/types";
import type { RpcClient } from "../rpc/client";
import { useTransportStore } from "../store/transportStore";

interface UseTimelineLoopDragParams {
  pps: number;
  transport: TransportSnapshot;
  rpc: RpcClient;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineLoopDragReturn {
  startLoopDrag: (which: "start" | "end") => (e: React.MouseEvent) => void;
  dispLoopStart: number;
  dispLoopEnd: number;
}

export function useTimelineLoopDrag({ pps, transport, rpc, tracksRef }: UseTimelineLoopDragParams): UseTimelineLoopDragReturn {
  const [loopDrag, setLoopDrag] = useState<"start" | "end" | null>(null);
  const [dragBeat, setDragBeat] = useState(0);
  const dragBeatRef = useRef(0);

  const dispLoopStart = loopDrag === "start" ? dragBeat : transport.loopStart;
  const dispLoopEnd = loopDrag === "end" ? dragBeat : transport.loopEnd;

  const startLoopDrag = useCallback((which: "start" | "end") => (e: React.MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    setLoopDrag(which);
    setDragBeat(which === "start" ? transport.loopStart : transport.loopEnd);

    const onMove = (ev: globalThis.MouseEvent) => {
      const rect = tracksRef.current?.getBoundingClientRect();
      if (!rect) return;
      const scroll = tracksRef.current?.scrollLeft ?? 0;
      const beat = Math.max(0, (ev.clientX - rect.left + scroll) / pps);
      dragBeatRef.current = beat;
      setDragBeat(beat);
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const finalBeat = dragBeatRef.current;
      const t = useTransportStore.getState().transport;
      useTransportStore.getState().update({
        ...t,
        loopStart: which === "start" ? finalBeat : t.loopStart,
        loopEnd: which === "end" ? finalBeat : t.loopEnd,
      });
      setLoopDrag(null);
      const method = which === "start" ? "project.setLoopStart" : "project.setLoopEnd";
      rpc.call(method, which === "start" ? { beat: finalBeat } : { beat: finalBeat }).catch(() => {});
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps, transport.loopStart, transport.loopEnd, rpc, tracksRef]);

  return { startLoopDrag, dispLoopStart, dispLoopEnd };
}
