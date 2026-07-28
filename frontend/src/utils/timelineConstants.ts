// frontend/src/utils/timelineConstants.ts

import type { RowLayout } from "./rowLayout";
import { rowAtY } from "./rowLayout";

// Vertical chrome shared by the timeline and the track-header column. The
// header column reproduces the toolbar + ruler band so its rows start at the
// exact same Y as the timeline lanes (see rowLayout.ts / TrackHeaders.tsx).
export const TOOLBAR_HEIGHT = 28;
export const RULER_HEIGHT = 24;
export const DEFAULT_PPS = 80; // pixels per second
export const MIN_PPS = 20;
export const MAX_PPS = 400;

export interface DragState {
  clipId: number;
  startTrackIndex: number;
  startBeat: number;
  offsetX: number;
  offsetY: number;
  mouseX: number;
  mouseY: number;
  isDuplicate?: boolean;
  isGhostClone?: boolean;
  paintRepeat?: boolean;
  paintOriginBeat: number;
  paintSpacing: number;
  paintedClipIds: number[];
}

export interface TrimState {
  clipId: number;
  side: "left" | "right";
  initialStartBeat: number;
  initialDuration: number;
  currentStartBeat: number;
  currentDuration: number;
}

export interface FadeDrag {
  clipId: number;
  side: "in" | "out";
  initialValue: number;
  startBeat: number;
  durationBeats: number;
}

export function computeRubberBandSelection(
  x1: number, y1: number, x2: number, y2: number,
  clips: Array<{ clipId: number; trackIndex: number; startBeat: number; durationBeats: number }>,
  pps: number,
  layout: RowLayout
): Set<number> {
  const selected = new Set<number>();
  if (layout.count === 0) return selected;
  const minBeat = Math.min(x1, x2) / pps;
  const maxBeat = Math.max(x1, x2) / pps;
  const minTrack = rowAtY(layout, Math.min(y1, y2));
  // Preserve the legacy `ceil(maxY / h) - 1` behaviour: a band whose bottom
  // edge lands exactly on a row boundary does not select the row below it.
  const maxTrack = rowAtY(layout, Math.max(y1, y2) - 1e-6);

  for (const clip of clips) {
    const clipEnd = clip.startBeat + clip.durationBeats;
    if (clip.startBeat <= maxBeat && clipEnd >= minBeat &&
        clip.trackIndex >= minTrack && clip.trackIndex <= maxTrack) {
      selected.add(clip.clipId);
    }
  }
  return selected;
}
