import { describe, it, expect } from "vitest";
import { computeCurrentStep, cellPitch, DRUM_LABELS } from "./StepSequencer";

const STEP_BEATS = 1 / 16;

describe("computeCurrentStep", () => {
  it("returns -1 for a negative beat", () => {
    expect(computeCurrentStep(-1, STEP_BEATS, 16)).toBe(-1);
  });

  it("maps beat 0 to step 0", () => {
    expect(computeCurrentStep(0, STEP_BEATS, 16)).toBe(0);
  });

  it("maps exactly one step in to step 1", () => {
    expect(computeCurrentStep(STEP_BEATS, STEP_BEATS, 16)).toBe(1);
  });

  it("wraps modulo the pattern length", () => {
    expect(computeCurrentStep(16 * STEP_BEATS, STEP_BEATS, 16)).toBe(0);
    expect(computeCurrentStep((16 + 5) * STEP_BEATS, STEP_BEATS, 16)).toBe(5);
  });

  it("respects a shorter pattern length", () => {
    expect(computeCurrentStep(8 * STEP_BEATS, STEP_BEATS, 8)).toBe(0);
    expect(computeCurrentStep(9 * STEP_BEATS, STEP_BEATS, 8)).toBe(1);
  });
});

describe("cellPitch", () => {
  it("row 0 is higher than row 1", () => {
    expect(cellPitch(0)).toBeGreaterThan(cellPitch(1));
  });
});

describe("DRUM_LABELS", () => {
  it("has a label per row, Kick on the bottom row", () => {
    expect(DRUM_LABELS.length).toBe(8);
    expect(DRUM_LABELS[7]).toBe("Kick");
  });
});
