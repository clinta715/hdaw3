// frontend/src/utils/timelineUtils.ts

import type { TrackSnapshot } from "../rpc/types";

export const AUDIO_EXTENSIONS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
export const MIDI_EXTENSIONS = [".mid", ".midi"];

// Tracks visible after collapsing folder children and hiding user-hidden tracks.
// The track-header column and the timeline lanes MUST use the same definition,
// or their rows (built from a shared RowLayout) would index different tracks
// and misalign.
export function getVisibleTracks(tracks: TrackSnapshot[]): TrackSnapshot[] {
  const hidden = new Set<number>();
  for (const track of tracks) {
    // User-hidden tracks
    if (track.isHidden) {
      hidden.add(track.index);
    }
    // Collapsed folder children
    if (track.trackType === 2 && track.isCollapsed) {
      for (const child of tracks) {
        if (child.parentId === track.index) hidden.add(child.index);
      }
    }
  }
  return tracks.filter((t) => !hidden.has(t.index));
}

export function isAudioFile(name: string): boolean {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return AUDIO_EXTENSIONS.includes(ext);
}

export function isMidiFile(name: string): boolean {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return MIDI_EXTENSIONS.includes(ext);
}

export function clientXToBeat(
  clientX: number,
  containerEl: HTMLElement,
  pps: number
): number {
  const rect = containerEl.getBoundingClientRect();
  const scroll = containerEl.scrollLeft;
  return Math.max(0, (clientX - rect.left + scroll) / pps);
}

import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "../components/snapUtils";

export function snapBeat(beat: number): number {
  const { snapEnabled, snapDivision } = useUiStore.getState();
  return snapEnabled ? snapToGrid(beat, snapDivision) : beat;
}

import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";

export async function syncAfterMutation(): Promise<void> {
  await useProjectStore.getState().syncDirtyFlag(rpc);
  await useProjectStore.getState().syncSnapshot(rpc);
}
