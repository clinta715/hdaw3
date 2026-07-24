import { useState, useRef, useCallback } from "react";
import type { ClipSnapshot } from "../rpc/types";
import type { RpcClient } from "../rpc/client";
import type { FadeDrag } from "../utils/timelineConstants";
import { useProjectStore } from "../store/projectStore";

interface UseTimelineFadeParams {
  clips: ClipSnapshot[];
  pps: number;
  rpc: RpcClient;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineFadeReturn {
  handleFadeStart: (e: React.MouseEvent, clip: ClipSnapshot, side: "in" | "out") => void;
  fadeDrag: FadeDrag | null;
}

export function useTimelineFade({ clips, pps, rpc, tracksRef }: UseTimelineFadeParams): UseTimelineFadeReturn {
  const [fadeDrag, setFadeDrag] = useState<FadeDrag | null>(null);
  const fadeDragRef = useRef(fadeDrag);
  fadeDragRef.current = fadeDrag;

  const handleFadeStart = useCallback((e: React.MouseEvent, clip: ClipSnapshot, side: "in" | "out") => {
    e.stopPropagation();
    e.preventDefault();
    setFadeDrag({
      clipId: clip.clipId,
      side,
      initialValue: side === "in" ? clip.fadeIn : clip.fadeOut,
      startBeat: clip.startBeat,
      durationBeats: clip.durationBeats,
    });

    const onMove = (ev: globalThis.MouseEvent) => {
      const el = tracksRef.current;
      if (!el) return;
      const rect = el.getBoundingClientRect();
      const scroll = el.scrollLeft;
      const d = fadeDragRef.current;
      if (!d) return;
      const clipStartPx = d.startBeat * pps;
      const clipEndPx = (d.startBeat + d.durationBeats) * pps;
      const mousePx = ev.clientX - rect.left + scroll;
      if (d.side === "in") {
        const newFade = Math.max(0, Math.min(d.durationBeats / 2, (mousePx - clipStartPx) / pps));
        setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
      } else {
        const newFade = Math.max(0, Math.min(d.durationBeats / 2, (clipEndPx - mousePx) / pps));
        setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
      }
    };

    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const d = fadeDragRef.current;
      if (d) {
        const fadedClip = d.side === "in" ? d.initialValue : undefined;
        const fadedOut = d.side === "out" ? d.initialValue : undefined;
        useProjectStore.setState((s) => {
          if (!s.snapshot) return {};
          return {
            snapshot: {
              ...s.snapshot,
              clips: s.snapshot.clips.map((c) =>
                c.clipId === d.clipId
                  ? {
                      ...c,
                      fadeIn: fadedClip !== undefined ? fadedClip : c.fadeIn,
                      fadeOut: fadedOut !== undefined ? fadedOut : c.fadeOut,
                    }
                  : c
              ),
            },
          };
        });

        const method = d.side === "in" ? "project.setClipFadeIn" : "project.setClipFadeOut";
        rpc.call(method, { clipId: d.clipId, [d.side === "in" ? "fadeIn" : "fadeOut"]: d.initialValue }).then(() => {
          // Snapshot updated optimistically above; the notify.treeChanged push
          // reconciles authoritative state ~16ms after the ValueTree mutation.
          useProjectStore.setState({ isDirty: true });
        }).catch(() => {});
      }
      setFadeDrag(null);
    };

    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [pps, rpc, tracksRef]);

  return { handleFadeStart, fadeDrag };
}
