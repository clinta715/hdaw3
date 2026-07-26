import { divisionToBeats } from "./snapUtils";

// Swing delays the off-beat (odd-indexed subdivision) notes. `swing` is in
// [0, 1]: 0 = straight, 1 = full swing where the off-beat moves to the triplet
// position (2/3 of the way to the next subdivision), i.e. an offset of
// gridBeats / 3. Downbeat (even-indexed) notes are left unchanged.
export function swingOffset(beat: number, gridBeats: number, swing: number): number {
  if (swing <= 0 || gridBeats <= 0) return 0;
  const index = Math.round(beat / gridBeats);
  if (index % 2 === 0) return 0;
  const s = Math.max(0, Math.min(1, swing));
  return s * (gridBeats / 3);
}

// Quantize a beat to the snap division, then apply swing, blending from the
// original position by `strength` (0 = unchanged, 1 = fully quantized+swung).
// With swing = 0 this is identical to a plain partial quantize.
export function quantizeWithGroove(
  beat: number,
  division: number,
  strength: number,
  swing: number,
): number {
  const grid = divisionToBeats(division);
  const snapped = Math.round(beat / grid) * grid;
  const target = snapped + swingOffset(snapped, grid, swing);
  return beat + (target - beat) * strength;
}

// A groove template names a swing amount. Swing is applied at whatever snap
// division is active; the template selects how hard the off-beats are pushed.
export interface GrooveTemplate {
  name: string;
  swing: number;
}

export const GROOVE_TEMPLATES: GrooveTemplate[] = [
  { name: "Straight", swing: 0 },
  { name: "Light Swing", swing: 0.33 },
  { name: "Swing", swing: 0.58 },
  { name: "Hard Swing", swing: 1.0 },
];
