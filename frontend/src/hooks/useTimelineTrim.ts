import { useState, useRef, useCallback } from "react";
import type { ClipSnapshot } from "../rpc/types";
import type { RpcClient } from "../rpc/client";
import type { TrimState } from "../utils/timelineConstants";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "../components/snapUtils";

interface UseTimelineTrimParams {
  clips: ClipSnapshot[];
  pps: number;
  rpc: RpcClient;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

interface UseTimelineTrimReturn {
  handleTrimStart: (e: React.MouseEvent, clip: ClipSnapshot, side: "left" | "right") => void;
  isTrimming: boolean;
  trimState: TrimState | null;
}

export function useTimelineTrim({ clips, pps, rpc, tracksRef }: UseTimelineTrimParams): UseTimelineTrimReturn {
  const [trimState, setTrimState] = useState<TrimState | null>(null);
  const trimRef = useRef<TrimState | null>(null);
  const updateTrim = useCallback((next: TrimState | null) => {
    trimRef.current = next;
    setTrimState(next);
  }, []);

  const handleTrimStart = useCallback(
    (e: React.MouseEvent, clip: ClipSnapshot, side: "left" | "right") => {
      e.stopPropagation();
      e.preventDefault();
      updateTrim({
        clipId: clip.clipId,
        side,
        initialStartBeat: clip.startBeat,
        initialDuration: clip.durationBeats,
        currentStartBeat: clip.startBeat,
        currentDuration: clip.durationBeats,
      });

      const onMove = (ev: globalThis.MouseEvent) => {
        const d = trimRef.current;
        if (!d) return;
        const el = tracksRef.current;
        if (!el) return;
        const rect = el.getBoundingClientRect();
        const scroll = el.scrollLeft;
        const rawMouseBeat = (ev.clientX - rect.left + scroll) / pps;
        const { snapEnabled, snapDivision } = useUiStore.getState();
        const mouseBeat = snapEnabled ? snapToGrid(rawMouseBeat, snapDivision) : rawMouseBeat;

        if (d.side === "left") {
          const maxStart = d.initialStartBeat + d.initialDuration - 0.5;
          const newStart = Math.max(0, Math.min(mouseBeat, maxStart));
          const newDuration = d.initialDuration + (d.initialStartBeat - newStart);
          updateTrim({ ...d, currentStartBeat: newStart, currentDuration: newDuration });
        } else {
          const newDuration = Math.max(0.5, mouseBeat - d.initialStartBeat);
          updateTrim({ ...d, currentDuration: newDuration });
        }
      };

      const onUp = () => {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        const d = trimRef.current;
        if (!d) return;

        const changed = d.side === "left"
          ? Math.abs(d.currentStartBeat - d.initialStartBeat) > 0.01
          : Math.abs(d.currentDuration - d.initialDuration) > 0.01;

        if (changed) {
          useProjectStore.setState((s) => {
            if (!s.snapshot) return {};
            return {
              snapshot: {
                ...s.snapshot,
                clips: s.snapshot.clips.map((c) =>
                  c.clipId === d.clipId
                    ? { ...c, startBeat: d.currentStartBeat, durationBeats: d.currentDuration }
                    : c
                ),
              },
            };
          });
        }

        updateTrim(null);

        if (changed) {
          if (d.side === "left") {
            (async () => {
              try {
                await rpc.call("project.beginTransaction", { name: "trim clip" });
                await rpc.call("project.setClipStart", { clipId: d.clipId, start: d.currentStartBeat });
                await rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration });
                await rpc.call("project.endTransaction");
                // Snapshot was updated optimistically above; the debounced
                // notify.treeChanged push reconciles authoritative state.
                useProjectStore.setState({ isDirty: true });
              } catch (e) { console.error("trim failed", e); }
            })();
          } else {
            (async () => {
              await rpc.call("project.setClipDuration", { clipId: d.clipId, duration: d.currentDuration }).catch(() => {});
              useProjectStore.setState({ isDirty: true });
            })();
          }
        }
      };

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
    [pps, updateTrim, rpc, tracksRef]
  );

  const isTrimming = trimState !== null;

  return { handleTrimStart, isTrimming, trimState };
}
