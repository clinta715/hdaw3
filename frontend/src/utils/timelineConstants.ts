// frontend/src/utils/timelineConstants.ts

export const TRACK_HEIGHT = 64;
export const RULER_HEIGHT = 32;
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
  pps: number
): Set<number> {
  const selected = new Set<number>();
  const minBeat = Math.min(x1, x2) / pps;
  const maxBeat = Math.max(x1, x2) / pps;
  const minTrack = Math.min(y1, y2) / TRACK_HEIGHT;
  const maxTrack = Math.max(y1, y2) / TRACK_HEIGHT;

  for (const clip of clips) {
    const clipEnd = clip.startBeat + clip.durationBeats;
    if (clip.startBeat <= maxBeat && clipEnd >= minBeat &&
        clip.trackIndex >= minTrack && clip.trackIndex <= maxTrack) {
      selected.add(clip.clipId);
    }
  }
  return selected;
}
