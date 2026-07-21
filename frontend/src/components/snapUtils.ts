export function snapToGrid(beat: number, division: number): number {
  // Grid size in beats. In 4/4, 1 bar = 4 beats and 1 beat = 1/4 note,
  // so the labels map to: bar=4, beat=1, 1/8=0.5, 1/16=0.25, 1/32=0.125.
  // (The previous array [1, 0.25, 0.125, 0.0625, 0.03125] was shifted 4x
  // too small — "Beat" actually snapped to 1/16 notes, which is why snap
  // appeared not to work.)
  const divs = [4, 1, 0.5, 0.25, 0.125]; // bar, beat, 1/8, 1/16, 1/32
  const grid = divs[division] ?? 1;
  return Math.round(beat / grid) * grid;
}
