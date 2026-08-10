import { useEffect } from "react";
import { useProjectStore } from "../../store/projectStore";
import { useTransportStore } from "../../store/transportStore";
import { useUiStore } from "../../store/uiStore";
import { rpc } from "../../rpc";
import { MIN_PPS, MAX_PPS } from "../../hooks/useTimelineZoom";

interface UseTimelineKeyboardOpts {
  handleDuplicateClip: () => void;
  pasteClipboard: () => void;
  setContextMenu: (menu: null) => void;
  setEmptyContextMenu: (menu: null) => void;
  tracksRef: React.RefObject<HTMLDivElement | null>;
  setPps: React.Dispatch<React.SetStateAction<number>>;
}

export function useTimelineKeyboard(opts: UseTimelineKeyboardOpts) {
  const {
    handleDuplicateClip,
    pasteClipboard,
    setContextMenu,
    setEmptyContextMenu,
    tracksRef,
    setPps,
  } = opts;

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      const tag = target?.tagName;
      if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA") return;
      // Skip when focus is inside a focusable custom widget (NoteGrid,
      // AutomationPanel) that handles its own key events. Prevents double-fire
      // (e.g. Delete deleting both notes and timeline clips).
      if (target !== document.body && target?.closest?.("[tabindex]")) return;

      const { selectedClipIds } = useUiStore.getState();
      const isPlaying = useTransportStore.getState().transport.isPlaying;

      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          (async () => {
            try {
              await rpc.call("project.removeClips", {
                clipIds: [...selectedClipIds],
              });
              useUiStore.getState().clearSelection();
              useProjectStore.setState({ isDirty: true });
            } catch (e) {
              console.error("Failed to delete clips:", e);
            }
          })();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyD") {
        e.preventDefault();
        handleDuplicateClip();
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyM") {
        e.preventDefault();
        const snap = useProjectStore.getState().snapshot;
        if (!snap) return;
        const selClips = snap.clips.filter((c) =>
          selectedClipIds.has(c.clipId)
        );
        const canMerge =
          selClips.length >= 2 &&
          selClips.every((c) => c.isMidi && !c.isGhost);
        if (canMerge) {
          const ids = selClips.map((c) => c.clipId);
          rpc
            .call("project.mergeClips", { clipIds: ids })
            .then((res) => {
              const newId = typeof res === "number" ? res : null;
              if (newId != null && newId > 0) {
                useUiStore.setState({
                  selectedClipIds: new Set([newId]),
                });
              }
              useProjectStore.setState({ isDirty: true });
            })
            .catch((err) => console.error("Merge failed:", err));
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyC") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          const snap = useProjectStore.getState().snapshot;
          if (snap) {
            const copied = snap.clips.filter((c) =>
              selectedClipIds.has(c.clipId)
            );
            useUiStore.getState().setClipboard(copied);
          }
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyX") {
        e.preventDefault();
        if (selectedClipIds.size > 0) {
          const snap = useProjectStore.getState().snapshot;
          if (snap) {
            const copied = snap.clips.filter((c) =>
              selectedClipIds.has(c.clipId)
            );
            useUiStore.getState().setClipboard(copied);
            (async () => {
              await rpc.call("project.removeClips", {
                clipIds: [...selectedClipIds],
              });
              useUiStore.getState().clearSelection();
              useProjectStore.setState({ isDirty: true });
            })();
          }
        }
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyV") {
        e.preventDefault();
        const { clipClipboard } = useUiStore.getState();
        if (clipClipboard.length > 0) {
          pasteClipboard();
        }
      } else if (e.key === "Escape") {
        setContextMenu(null);
        setEmptyContextMenu(null);
      } else if (e.code === "KeyF" && e.shiftKey) {
        const snap = useProjectStore.getState().snapshot;
        const selIds = useUiStore.getState().selectedClipIds;
        const selectedClips = snap?.clips.filter((c) =>
          selIds.has(c.clipId)
        );
        if (selectedClips && selectedClips.length > 0) {
          const minStart = Math.min(
            ...selectedClips.map((c) => c.startBeat)
          );
          const maxEnd = Math.max(
            ...selectedClips.map((c) => c.startBeat + c.durationBeats)
          );
          const range = maxEnd - minStart;
          if (range > 0) {
            const cw = tracksRef.current?.clientWidth ?? 800;
            const newPps = (cw * 0.8) / range;
            setPps(Math.max(MIN_PPS, Math.min(MAX_PPS, newPps)));
            requestAnimationFrame(() => {
              if (tracksRef.current) {
                tracksRef.current.scrollLeft = minStart * newPps - cw * 0.1;
              }
            });
          }
        }
        e.preventDefault();
      } else if (e.key === " ") {
        e.preventDefault();
        if (isPlaying) rpc.call("transport.stop").catch(() => {});
        else rpc.call("transport.play").catch(() => {});
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [
    handleDuplicateClip,
    pasteClipboard,
    setContextMenu,
    setEmptyContextMenu,
    tracksRef,
    setPps,
  ]);
}
