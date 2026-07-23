import { useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useMarkerStore } from "../store/markerStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import type { ClipSnapshot } from "../rpc/types";
import type { MarkerSnapshot } from "../store/markerStore";
import type { TransportSnapshot } from "../rpc/types";

interface TimelineContextMenuProps {
  contextMenu: { x: number; y: number; type: string; clip?: ClipSnapshot; markerIndex?: number } | null;
  emptyContextMenu: { x: number; y: number; beat: number } | null;
  clips: ClipSnapshot[];
  markers: MarkerSnapshot[];
  selectedClipIds: Set<number>;
  transport: TransportSnapshot;
  onClose: () => void;
  onDeleteClip: () => void;
  onDuplicateClip: () => void;
  onSplitClip: () => void;
}

export function TimelineContextMenu({
  contextMenu,
  emptyContextMenu,
  clips,
  markers,
  selectedClipIds,
  transport,
  onClose,
  onDeleteClip,
  onDuplicateClip,
  onSplitClip,
}: TimelineContextMenuProps) {
  const pasteClipboard = useCallback(async () => {
    const { clipClipboard } = useUiStore.getState();
    if (clipClipboard.length === 0) return;
    const tr = useTransportStore.getState().transport;
    const playheadBeats = tr.currentTimeSeconds * (tr.bpm / 60);
    const minStart = Math.min(...clipClipboard.map((c) => c.startBeat));
    await rpc.call("project.beginTransaction", { name: "paste clips" });
    for (const clip of clipClipboard) {
      const newStart = playheadBeats + (clip.startBeat - minStart);
      if (clip.isMidi) {
        await rpc.call("project.addMidiClip", { trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, name: clip.name }).catch(() => {});
      } else {
        await rpc.call("project.addAudioClip", { trackIndex: clip.trackIndex, start: newStart, duration: clip.durationBeats, sourceFile: clip.sourceFile, name: clip.name }).catch(() => {});
      }
    }
    await rpc.call("project.endTransaction");
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  }, []);

  if (!contextMenu && !emptyContextMenu) return null;

  return (
    <>
      {contextMenu && (
        <div
          className="clip-context-menu"
          onClick={(e) => e.stopPropagation()}
          style={{ left: contextMenu.x, top: contextMenu.y }}
        >
          {contextMenu.type === "clip" && contextMenu.clip && (
            <>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                onClose();
                onDeleteClip();
              }}>
                Delete
              </button>
              <button onMouseDown={(e) => { e.stopPropagation(); onClose(); onDuplicateClip(); }}>
                Duplicate
              </button>
              <div className="ctx-separator" />
              <button
                className={contextMenu.clip.looping ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  const clipId = contextMenu.clip!.clipId;
                  const newLooping = !contextMenu.clip!.looping;
                  rpc.call("project.setClipLooping", { clipId, looping: newLooping }).then(() => {
                    useProjectStore.getState().syncSnapshot(rpc);
                  });
                  onClose();
                }}
              >
                {contextMenu.clip.looping ? "✓ " : ""}Looped
              </button>
              <button
                className={contextMenu.clip.muted ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  const clipId = contextMenu.clip!.clipId;
                  const newMuted = !contextMenu.clip!.muted;
                  rpc.call("project.setClipMuted", { clipId, muted: newMuted }).then(() => {
                    useProjectStore.getState().syncSnapshot(rpc);
                  });
                  onClose();
                }}
              >
                {contextMenu.clip.muted ? "✓ " : ""}Muted
              </button>
              <div className="ctx-separator" />
              {contextMenu.clip.isGhost && contextMenu.clip.ghostSourceId >= 0 && (
                <button onMouseDown={(e) => {
                  e.stopPropagation();
                  onClose();
                  const sourceClip = clips.find((c) => c.clipId === contextMenu.clip!.ghostSourceId);
                  if (sourceClip) {
                    useUiStore.getState().selectClip(sourceClip.clipId, sourceClip.trackIndex);
                  }
                }}>
                  Select Original
                </button>
              )}
              <button onMouseDown={(e) => { e.stopPropagation(); onClose(); onSplitClip(); }}>
                Split
              </button>
              <button onMouseDown={(e) => { e.stopPropagation(); useUiStore.getState().setClipboard([contextMenu.clip!]); onClose(); }}>
                Copy
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                useUiStore.getState().setClipboard([contextMenu.clip!]);
                onClose();
                rpc.call("project.beginTransaction", { name: "cut clip" }).then(() =>
                  rpc.call("project.removeClip", { clipId: contextMenu.clip!.clipId })
                ).then(() => rpc.call("project.endTransaction")).then(() => {
                  useProjectStore.getState().syncDirtyFlag(rpc);
                  useProjectStore.getState().syncSnapshot(rpc);
                }).catch(() => {});
              }}>
                Cut
              </button>
              <div className="ctx-separator" />
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.sliceClipAtPlayhead", { clipId });
                onClose();
              }}>
                Slice at Playhead
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.sliceClipAtTransients", { clipId });
                onClose();
              }}>
                Slice at Transients
              </button>
              <div className="ctx-separator" />
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.copyAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
                onClose();
              }}>
                Copy Region
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.cutAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
                onClose();
              }}>
                Cut Region
              </button>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const clipId = [...selectedClipIds][0];
                rpc.call("project.pasteAudioClipRegion", { clipId, pasteTime: transport.currentTimeSeconds });
                onClose();
              }}>
                Paste Region
              </button>
            </>
          )}
          {contextMenu.type === "marker" && (
            <>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                const marker = markers.find((m) => m.index === contextMenu.markerIndex);
                const name = prompt("Marker name:", marker?.name ?? "");
                if (name != null && contextMenu.markerIndex != null) {
                  rpc.call("project.setMarkerName", { index: contextMenu.markerIndex, name }).then(() => {
                    useMarkerStore.getState().syncMarkers(rpc);
                  }).catch(() => {});
                }
                onClose();
              }}>
                Rename Marker
              </button>
              <button className="ctx-danger" onMouseDown={(e) => {
                e.stopPropagation();
                if (contextMenu.markerIndex != null) {
                  rpc.call("project.removeMarker", { index: contextMenu.markerIndex }).then(() => {
                    useMarkerStore.getState().syncMarkers(rpc);
                  }).catch(() => {});
                }
                onClose();
              }}>
                Delete Marker
              </button>
            </>
          )}
        </div>
      )}

      {emptyContextMenu && (
        <div className="clip-context-menu" style={{ left: emptyContextMenu.x, top: emptyContextMenu.y }}
          onMouseDown={(e) => e.stopPropagation()}>
          <button onMouseDown={(e) => { e.stopPropagation(); rpc.call("project.addTrack").catch(() => {}); onClose(); }}>
            Add Track
          </button>
          {useUiStore.getState().clipClipboard.length > 0 && (
            <button onMouseDown={(e) => {
              e.stopPropagation();
              onClose();
              pasteClipboard();
            }}>
              Paste
            </button>
          )}
          <button onMouseDown={(e) => {
            e.stopPropagation();
            const bpm = prompt("BPM:", "120");
            if (bpm) rpc.call("project.setTempo", { bpm: parseFloat(bpm) || 120 }).catch(() => {});
            onClose();
          }}>
            Set Global BPM...
          </button>
          <button onMouseDown={(e) => {
            e.stopPropagation();
            rpc.call("project.addMidiClip", {
              trackIndex: 0,
              start: emptyContextMenu.beat,
              duration: 4,
              name: "New MIDI Clip",
            }).catch(() => {});
            onClose();
          }}>
            Add MIDI Clip
          </button>
        </div>
      )}
    </>
  );
}
