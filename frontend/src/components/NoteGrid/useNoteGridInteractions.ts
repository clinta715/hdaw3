import { useCallback } from "react";
import { NoteSnapshot } from "../../rpc/types";
import { RpcClient } from "../../rpc/client";
import { useProjectStore } from "../../store/projectStore";
import { useUiStore } from "../../store/uiStore";
import { snap } from "../snapUtils";
import { quantizeWithGroove } from "../grooveUtils";
import { ContextMenuState } from "./noteGridTypes";
import { KEY_HEIGHT, clamp, noteClipboardState } from "./noteGridConstants";

interface UseNoteGridInteractionsOptions {
  notes: NoteSnapshot[];
  noteMap: Map<number, NoteSnapshot>;
  rpc: RpcClient;
  clipId: number | null;
  chordShape?: number[];
  quantizeStrength: number;
  swing: number;
  selectedNoteIds: Set<number>;
  setSelectedNoteIds: (ids: Set<number> | ((prev: Set<number>) => Set<number>)) => void;
  setContextMenu: React.Dispatch<React.SetStateAction<ContextMenuState | null>>;
  gridRef: React.RefObject<HTMLDivElement | null>;
  ppbRef: React.RefObject<number>;
}

export function useNoteGridInteractions({
  notes,
  noteMap,
  rpc,
  clipId,
  chordShape,
  quantizeStrength,
  swing,
  selectedNoteIds,
  setSelectedNoteIds,
  setContextMenu,
  gridRef,
  ppbRef,
}: UseNoteGridInteractionsOptions) {
  const handleDoubleClick = useCallback(
    async (e: React.MouseEvent) => {
      if (clipId == null) return;
      if ((e.target as HTMLElement).closest(".ng-note")) return;

      const gridEl = gridRef.current;
      if (!gridEl) return;
      const rect = gridEl.getBoundingClientRect();
      const x = e.clientX - rect.left + gridEl.scrollLeft;
      const y = e.clientY - rect.top + gridEl.scrollTop;
      const pitch = clamp(127 - Math.floor(y / KEY_HEIGHT), 0, 127);
      const rawBeat = x / ppbRef.current;
      const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } =
        useUiStore.getState();
      const startBeat = snap(rawBeat, {
        enabled: snapEnabled,
        division: snapDivision,
        gridOffset: snapGridOffset,
        events: snapToEvents,
      });

      const newNotes: NoteSnapshot[] = [
        {
          noteId: Date.now(),
          pitch,
          velocity: 100,
          startBeat,
          durationBeats: 0.25,
        },
      ];
      if (chordShape && chordShape.length > 0) {
        for (const interval of chordShape.slice(1)) {
          newNotes.push({
            noteId: Date.now() + interval,
            pitch: pitch + interval,
            velocity: 100,
            startBeat,
            durationBeats: 0.25,
          });
        }
      }
      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId) ?? [];
        return {
          notesByClip: new Map(s.notesByClip).set(clipId, [
            ...arr,
            ...newNotes,
          ]),
        };
      });

      try {
        await rpc.call("project.addNote", {
          clipId,
          pitch,
          startBeat,
          durationBeats: 0.25,
          velocity: 100,
        });
        if (chordShape && chordShape.length > 0) {
          for (const interval of chordShape.slice(1)) {
            await rpc.call("project.addNote", {
              clipId,
              pitch: pitch + interval,
              startBeat,
              durationBeats: 0.25,
              velocity: 100,
            });
          }
        }
        useProjectStore.getState().syncNotes(rpc, clipId);
      } catch (err) {
        console.warn("note creation failed", err);
      }
    },
    [rpc, clipId, chordShape, gridRef, ppbRef]
  );

  const deleteSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;

    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.filter((n) => !selectedNoteIds.has(n.noteId))
        ),
      };
    });
    setSelectedNoteIds(new Set());

    try {
      if (selectedNoteIds.size > 1) {
        await rpc.call("project.beginTransaction", { name: "delete notes" });
      }
      for (const noteId of selectedNoteIds) {
        await rpc.call("project.removeNote", { noteId });
      }
      if (selectedNoteIds.size > 1) {
        await rpc.call("project.endTransaction");
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("note deletion failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, setSelectedNoteIds]);

  const transposeSelected = useCallback(
    async (semitones: number) => {
      if (clipId == null || selectedNoteIds.size === 0) return;

      useProjectStore.setState((s) => {
        const arr = s.notesByClip.get(clipId);
        if (!arr) return {};
        return {
          notesByClip: new Map(s.notesByClip).set(
            clipId,
            arr.map((n) =>
              selectedNoteIds.has(n.noteId)
                ? { ...n, pitch: clamp(n.pitch + semitones, 0, 127) }
                : n
            )
          ),
        };
      });

      try {
        for (const noteId of selectedNoteIds) {
          const note = noteMap.get(noteId);
          if (!note) continue;
          const newPitch = clamp(note.pitch + semitones, 0, 127);
          await rpc.call("project.setNotePitch", { noteId, pitch: newPitch });
        }
        useProjectStore.getState().syncNotes(rpc, clipId);
      } catch (err) {
        console.warn("transpose failed", err);
      }
    },
    [selectedNoteIds, rpc, clipId, noteMap]
  );

  const quantizeSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    if (!snapEnabled) return;

    const strength = quantizeStrength / 100;

    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.map((n) => {
            if (!selectedNoteIds.has(n.noteId)) return n;
            const newStart = quantizeWithGroove(
              n.startBeat,
              snapDivision,
              strength,
              swing
            );
            return { ...n, startBeat: newStart };
          })
        ),
      };
    });

    try {
      for (const noteId of selectedNoteIds) {
        const note = noteMap.get(noteId);
        if (!note) continue;
        const newStart = quantizeWithGroove(
          note.startBeat,
          snapDivision,
          strength,
          swing
        );
        await rpc.call("project.setNoteStart", {
          noteId,
          startBeat: newStart,
        });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("quantize failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, noteMap, quantizeStrength, swing]);

  const humanizeSelected = useCallback(async () => {
    if (clipId == null || selectedNoteIds.size === 0) return;

    const offsets = new Map<
      number,
      { beatOffset: number; velOffset: number }
    >();
    for (const noteId of selectedNoteIds) {
      offsets.set(noteId, {
        beatOffset: (Math.random() - 0.5) * 0.06,
        velOffset: Math.round((Math.random() - 0.5) * 10),
      });
    }

    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId);
      if (!arr) return {};
      return {
        notesByClip: new Map(s.notesByClip).set(
          clipId,
          arr.map((n) => {
            const offset = offsets.get(n.noteId);
            if (!offset) return n;
            return {
              ...n,
              startBeat: Math.max(0, n.startBeat + offset.beatOffset),
              velocity: clamp(n.velocity + offset.velOffset, 1, 127),
            };
          })
        ),
      };
    });

    try {
      for (const noteId of selectedNoteIds) {
        const note = noteMap.get(noteId);
        if (!note) continue;
        const offset = offsets.get(noteId)!;
        const newStart = Math.max(0, note.startBeat + offset.beatOffset);
        const newVel = clamp(note.velocity + offset.velOffset, 1, 127);
        await rpc.call("project.setNoteStart", {
          noteId,
          startBeat: newStart,
        });
        await rpc.call("project.setNoteVelocity", {
          noteId,
          velocity: newVel,
        });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("humanize failed", err);
    }
  }, [selectedNoteIds, rpc, clipId, noteMap]);

  const copySelected = useCallback(() => {
    if (selectedNoteIds.size === 0) return;
    noteClipboardState.items = notes
      .filter((n) => selectedNoteIds.has(n.noteId))
      .map((n) => ({ ...n }));
  }, [selectedNoteIds, notes]);

  const cutSelected = useCallback(async () => {
    copySelected();
    await deleteSelected();
  }, [copySelected, deleteSelected]);

  const pasteAtScroll = useCallback(async () => {
    if (clipId == null || noteClipboardState.items.length === 0) return;
    const gridEl = gridRef.current;
    const scrollBeat = gridEl ? gridEl.scrollLeft / ppbRef.current : 0;
    const minBeat = Math.min(
      ...noteClipboardState.items.map((n) => n.startBeat)
    );

    const pastedNotes: NoteSnapshot[] = noteClipboardState.items.map(
      (n, i) => ({
        ...n,
        noteId: Date.now() + i,
        startBeat: n.startBeat - minBeat + scrollBeat,
      })
    );
    useProjectStore.setState((s) => {
      const arr = s.notesByClip.get(clipId) ?? [];
      return {
        notesByClip: new Map(s.notesByClip).set(clipId, [
          ...arr,
          ...pastedNotes,
        ]),
      };
    });

    try {
      for (const n of noteClipboardState.items) {
        const startBeat = n.startBeat - minBeat + scrollBeat;
        await rpc.call("project.addNote", {
          clipId,
          pitch: n.pitch,
          startBeat,
          durationBeats: n.durationBeats,
          velocity: n.velocity,
        });
      }
      useProjectStore.getState().syncNotes(rpc, clipId);
    } catch (err) {
      console.warn("paste failed", err);
    }
  }, [rpc, clipId, gridRef, ppbRef]);

  const selectAll = useCallback(() => {
    setSelectedNoteIds(new Set(notes.map((n) => n.noteId)));
  }, [notes, setSelectedNoteIds]);

  const handleKeyDown = useCallback(
    async (e: React.KeyboardEvent) => {
      if (clipId == null) return;
      setContextMenu(null);

      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        await deleteSelected();
        return;
      }
      if (e.key === "ArrowUp") {
        e.preventDefault();
        await transposeSelected(e.ctrlKey ? 12 : 1);
        return;
      }
      if (e.key === "ArrowDown") {
        e.preventDefault();
        await transposeSelected(e.ctrlKey ? -12 : -1);
        return;
      }
      if (e.code === "KeyQ") {
        e.preventDefault();
        await quantizeSelected();
        return;
      }
      if (e.code === "KeyH") {
        e.preventDefault();
        await humanizeSelected();
        return;
      }
      if (e.ctrlKey || e.metaKey) {
        if (e.code === "KeyC") {
          e.preventDefault();
          copySelected();
          return;
        }
        if (e.code === "KeyX") {
          e.preventDefault();
          await cutSelected();
          return;
        }
        if (e.code === "KeyV") {
          e.preventDefault();
          await pasteAtScroll();
          return;
        }
        if (e.code === "KeyA") {
          e.preventDefault();
          selectAll();
          return;
        }
      }
    },
    [
      clipId,
      deleteSelected,
      transposeSelected,
      quantizeSelected,
      humanizeSelected,
      copySelected,
      cutSelected,
      pasteAtScroll,
      selectAll,
      setContextMenu,
    ]
  );

  return {
    handleDoubleClick,
    deleteSelected,
    transposeSelected,
    quantizeSelected,
    humanizeSelected,
    copySelected,
    cutSelected,
    pasteAtScroll,
    selectAll,
    handleKeyDown,
  };
}
