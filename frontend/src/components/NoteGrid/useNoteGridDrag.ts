import { useCallback, useEffect } from "react";
import { NoteSnapshot } from "../../rpc/types";
import { RpcClient } from "../../rpc/client";
import { useProjectStore } from "../../store/projectStore";
import { useUiStore } from "../../store/uiStore";
import { snap } from "../snapUtils";
import { NoteDragState, NoteResizeState } from "./noteGridTypes";
import { KEY_HEIGHT, clamp } from "./noteGridConstants";

interface UseNoteGridDragOptions {
  dragRef: React.RefObject<NoteDragState | null>;
  resizeRef: React.RefObject<NoteResizeState | null>;
  ppbRef: React.RefObject<number>;
  notesRef: React.RefObject<NoteSnapshot[]>;
  setDragState: React.Dispatch<React.SetStateAction<NoteDragState | null>>;
  setResizeState: React.Dispatch<React.SetStateAction<NoteResizeState | null>>;
  rpc: RpcClient;
  clipId: number | null;
  autoScroll: { update: (x: number, y: number) => void; stop: () => void };
  dragState: NoteDragState | null;
  resizeState: NoteResizeState | null;
}

export function useNoteGridDrag({
  dragRef,
  resizeRef,
  ppbRef,
  setDragState,
  setResizeState,
  rpc,
  clipId,
  autoScroll,
  dragState,
  resizeState,
}: UseNoteGridDragOptions) {
  const handleMouseMove = useCallback(
    (e: globalThis.MouseEvent) => {
      autoScroll.update(e.clientX, e.clientY);
      const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } =
        useUiStore.getState();
      const settings = {
        enabled: snapEnabled,
        division: snapDivision,
        gridOffset: snapGridOffset,
        events: snapToEvents,
      };

      setResizeState((prev) => {
        if (!prev) return null;
        const deltaX = e.clientX - prev.startX;
        const rawDuration = Math.max(
          0.03125,
          prev.initialDuration + deltaX / ppbRef.current
        );
        const newDuration = Math.max(0.03125, snap(rawDuration, settings));
        return { ...prev, currentDuration: newDuration };
      });

      setDragState((prev) => {
        if (!prev) return null;
        const pitchDelta = clamp(
          -Math.round((e.clientY - prev.offsetY) / KEY_HEIGHT),
          -prev.minPitch,
          127 - prev.maxPitch
        );
        const anchor = prev.members[prev.anchorIndex];
        const rawStart = Math.max(
          0,
          anchor.startBeat + (e.clientX - prev.offsetX) / ppbRef.current
        );
        const snappedAnchorStart = snap(rawStart, settings, {
          originalStart: anchor.startBeat,
        });
        let beatDelta = snappedAnchorStart - anchor.startBeat;
        if (prev.minBeat + beatDelta < 0) beatDelta = -prev.minBeat;
        return { ...prev, pitchDelta, beatDelta };
      });
    },
    [autoScroll, setResizeState, setDragState, ppbRef]
  );

  const handleMouseUp = useCallback(async () => {
    autoScroll.stop();
    const drag = dragRef.current;
    const resize = resizeRef.current;

    if ((drag || resize) && clipId != null) {
      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId);
        if (!arr) return {};
        const memberMap = drag
          ? new Map(drag.members.map((m) => [m.noteId, m]))
          : null;
        return {
          notesByClip: new Map(s.notesByClip).set(
            clipId,
            arr.map((n) => {
              const member = memberMap?.get(n.noteId);
              if (member) {
                return {
                  ...n,
                  pitch: clamp(member.startPitch + drag!.pitchDelta, 0, 127),
                  startBeat: Math.max(
                    0,
                    member.startBeat + drag!.beatDelta
                  ),
                };
              }
              if (resize && n.noteId === resize.noteId) {
                return { ...n, durationBeats: resize.currentDuration };
              }
              return n;
            })
          ),
        };
      });
    }

    try {
      if (drag && clipId != null) {
        if (drag.pitchDelta !== 0 || drag.beatDelta !== 0) {
          if (drag.members.length > 1) {
            await rpc.call("project.beginTransaction", {
              name: "move notes",
            });
          }
          for (const m of drag.members) {
            await rpc.call("project.setNotePitch", {
              noteId: m.noteId,
              pitch: clamp(m.startPitch + drag.pitchDelta, 0, 127),
            });
            await rpc.call("project.setNoteStart", {
              noteId: m.noteId,
              startBeat: Math.max(0, m.startBeat + drag.beatDelta),
            });
          }
          if (drag.members.length > 1) {
            await rpc.call("project.endTransaction");
          }
        }
      }
      if (resize && clipId != null) {
        const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } =
          useUiStore.getState();
        const snappedDuration = Math.max(
          0.03125,
          snap(resize.currentDuration, {
            enabled: snapEnabled,
            division: snapDivision,
            gridOffset: snapGridOffset,
            events: snapToEvents,
          })
        );
        await rpc.call("project.setNoteDuration", {
          noteId: resize.noteId,
          durationBeats: snappedDuration,
        });
      }
      if ((drag || resize) && clipId != null) {
        useProjectStore.getState().syncNotes(rpc, clipId);
      }
    } catch (err) {
      console.warn("note interaction failed", err);
    }
    useUiStore.getState().setStatusHint(null);
    setDragState(null);
    setResizeState(null);
  }, [autoScroll, dragRef, resizeRef, rpc, clipId, setDragState, setResizeState]);

  // Window-level drag listeners installed at note mousedown and removed on
  // release. Element-level handlers miss events once the cursor leaves the grid.
  useEffect(() => {
    const move = (e: globalThis.MouseEvent) => handleMouseMove(e);
    const up = () => {
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
      handleMouseUp();
    };
    if (dragState || resizeState) {
      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", up);
      return () => {
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
      };
    }
  }, [dragState, resizeState, handleMouseMove, handleMouseUp]);

  return { handleMouseMove, handleMouseUp };
}
