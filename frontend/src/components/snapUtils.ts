export function snapToGrid(beat: number, division: number): number {
  const divs = [1, 0.25, 0.125, 0.0625, 0.03125]; // bar, beat, 1/8, 1/16, 1/32
  const grid = divs[division] ?? 0.25;
  return Math.round(beat / grid) * grid;
}
