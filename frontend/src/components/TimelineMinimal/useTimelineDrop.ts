import { useCallback } from "react";
import { rpc } from "../../rpc";
import { useProjectStore } from "../../store/projectStore";
import { useUiStore } from "../../store/uiStore";
import { snap } from "../snapUtils";
import { buildRowLayout, rowAtYOrCount } from "../../utils/rowLayout";

interface UseTimelineDropOpts {
  pps: number;
  layout: ReturnType<typeof buildRowLayout>;
  tracksRef: React.RefObject<HTMLDivElement | null>;
}

export function useTimelineDrop(opts: UseTimelineDropOpts) {
  const { pps, layout, tracksRef } = opts;

  const handleDrop = useCallback(
    (e: React.DragEvent) => {
      e.preventDefault();

      // Check for internal file browser drag
      const hdawData = e.dataTransfer.getData("application/hdaw-file");
      if (hdawData) {
        try {
          const { path: filePath, name: fileName } = JSON.parse(hdawData);
          const rect = e.currentTarget.getBoundingClientRect();
          const y = e.clientY - rect.top + (tracksRef.current?.scrollTop ?? 0);
          const trackIdx = rowAtYOrCount(layout, y);
          const elScroll = tracksRef.current?.scrollLeft ?? 0;
          const beatX = (e.clientX - rect.left + elScroll) / pps;
          const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } =
            useUiStore.getState();
          const startBeat = snap(beatX, {
            enabled: snapEnabled,
            division: snapDivision,
            gridOffset: snapGridOffset,
            events: snapToEvents,
          });

          const ext = "." + fileName.split(".").pop()?.toLowerCase();
          const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
          const midiExts = [".mid", ".midi"];

          const doImport = async (targetTrack: number) => {
            if (audioExts.includes(ext)) {
              await rpc.call("project.addAudioClip", {
                trackIndex: targetTrack,
                start: Math.max(0, startBeat),
                duration: 4,
                sourceFile: filePath,
                name: fileName,
              });
            } else if (midiExts.includes(ext)) {
              await rpc.call("project.importMidiFile", {
                filePath,
                trackIndex: targetTrack,
              });
            }
          };

          const currentTracks =
            useProjectStore.getState().snapshot?.tracks ?? [];
          if (
            trackIdx >= currentTracks.length &&
            (audioExts.includes(ext) || midiExts.includes(ext))
          ) {
            const trackType = midiExts.includes(ext) ? 1 : 0;
            rpc
              .call("project.addTrack", { trackType })
              .then((newIdx) => {
                doImport(
                  typeof newIdx === "number" ? newIdx : currentTracks.length
                );
              })
              .catch(() => {});
          } else {
            doImport(
              Math.max(0, Math.min(trackIdx, currentTracks.length - 1))
            );
          }
        } catch {}
        return;
      }

      // External file drop (from OS)
      const files = Array.from(e.dataTransfer.files);
      const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
      const midiExts = [".mid", ".midi"];
      const rect = e.currentTarget.getBoundingClientRect();
      const y = e.clientY - rect.top + (tracksRef.current?.scrollTop ?? 0);
      const trackIdx = rowAtYOrCount(layout, y);
      const elScroll = tracksRef.current?.scrollLeft ?? 0;
      const beatX = (e.clientX - rect.left + elScroll) / pps;
      const { snapEnabled, snapDivision, snapGridOffset, snapToEvents } =
        useUiStore.getState();
      const startBeat = snap(beatX, {
        enabled: snapEnabled,
        division: snapDivision,
        gridOffset: snapGridOffset,
        events: snapToEvents,
      });
      const currentTracks =
        useProjectStore.getState().snapshot?.tracks ?? [];

      for (const file of files) {
        const ext = "." + file.name.split(".").pop()?.toLowerCase();
        const isAudio = audioExts.includes(ext);
        const isMidi = midiExts.includes(ext);
        if (!isAudio && !isMidi) continue;

        const doImport = async (targetTrack: number) => {
          if (isAudio) {
            await rpc.call("project.addAudioClip", {
              trackIndex: targetTrack,
              start: Math.max(0, startBeat),
              duration: 4,
              sourceFile: (file as any).path ?? file.name,
              name: file.name,
            });
          } else {
            await rpc.call("project.importMidiFile", {
              filePath: (file as any).path ?? file.name,
              trackIndex: targetTrack,
            });
          }
        };

        if (trackIdx >= currentTracks.length) {
          const trackType = isMidi ? 1 : 0;
          rpc
            .call("project.addTrack", { trackType })
            .then((newIdx) => {
              doImport(
                typeof newIdx === "number" ? newIdx : currentTracks.length
              );
            })
            .catch(() => {});
        } else {
          doImport(
            Math.max(0, Math.min(trackIdx, currentTracks.length - 1))
          );
        }
      }
    },
    [pps, layout, tracksRef]
  );

  return { handleDrop };
}
