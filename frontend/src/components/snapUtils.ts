// Grid size in beats for a snap-division index. In 4/4, 1 bar = 4 beats and
// 1 beat = 1/4 note, so the labels map to: bar=4, beat=1, 1/8=0.5, 1/16=0.25,
// 1/32=0.125. (The previous array [1, 0.25, 0.125, 0.0625, 0.03125] was shifted
// 4x too small — "Beat" actually snapped to 1/16 notes, which is why snap
// appeared not to work.)
const DIVS = [4, 1, 0.5, 0.25, 0.125]; // bar, beat, 1/8, 1/16, 1/32

export function divisionToBeats(division: number): number {
  return DIVS[division] ?? 1;
}

export function snapToGrid(beat: number, division: number): number {
  const grid = divisionToBeats(division);
  return Math.round(beat / grid) * grid;
}

export interface SnapSettings {
  enabled: boolean;
  division: number;
  gridOffset?: boolean;
  events?: boolean;
  eventThresholdBeats?: number;
}

export interface SnapContext {
  originalStart?: number;
  edges?: number[];
}

// Snap the DELTA from the gesture's original start, preserving the item's
// existing sub-grid offset (Bitwig "Grid Offset" mode).
export function snapWithOffset(beat: number, originalStart: number, division: number): number {
  return originalStart + snapToGrid(beat - originalStart, division);
}

// Nearest neighbor edge within thresholdBeats, or null if none is close enough.
export function snapToEvents(beat: number, edges: number[], thresholdBeats: number): number | null {
  let best: number | null = null;
  let bestDist = Infinity;
  for (const edge of edges) {
    const dist = Math.abs(edge - beat);
    if (dist <= thresholdBeats && dist < bestDist) {
      bestDist = dist;
      best = edge;
    }
  }
  return best;
}

// Composable snap facade. With only grid enabled (defaults) this is exactly
// snapToGrid(beat, division). When multiple anchors are active, the candidate
// producing the smallest move from `beat` wins (closest anchor).
export function snap(beat: number, settings: SnapSettings, ctx: SnapContext = {}): number {
  if (!settings.enabled) return beat;

  const gridCandidate =
    settings.gridOffset && ctx.originalStart != null
      ? snapWithOffset(beat, ctx.originalStart, settings.division)
      : snapToGrid(beat, settings.division);

  if (settings.events && ctx.edges && ctx.edges.length > 0) {
    const threshold = settings.eventThresholdBeats ?? 0.06;
    const eventCandidate = snapToEvents(beat, ctx.edges, threshold);
    if (eventCandidate != null) {
      return Math.abs(eventCandidate - beat) <= Math.abs(gridCandidate - beat)
        ? eventCandidate
        : gridCandidate;
    }
  }

  return gridCandidate;
}

// Build a sorted list of neighbor start/end edges from a clip list, excluding
// the dragged clip(s). Pure helper for feeding `edges` into snap().
export function collectClipEdges(
  clips: Array<{ startBeat: number; durationBeats: number }>,
  excludeIds?: Set<number | undefined>,
  idOf?: (c: any) => number | undefined
): number[] {
  const edges: number[] = [];
  for (const c of clips) {
    const id = idOf ? idOf(c) : (c as { clipId?: number }).clipId;
    if (excludeIds && excludeIds.has(id)) continue;
    edges.push(c.startBeat);
    edges.push(c.startBeat + c.durationBeats);
  }
  return edges.sort((a, b) => a - b);
}
