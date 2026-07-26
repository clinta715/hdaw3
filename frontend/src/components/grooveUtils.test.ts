import { describe, it, expect } from "vitest";
import { swingOffset, quantizeWithGroove, GROOVE_TEMPLATES } from "./grooveUtils";

describe("swingOffset", () => {
  it("returns 0 for straight (swing 0)", () => {
    expect(swingOffset(0.5, 0.5, 0)).toBe(0);
  });

  it("leaves downbeat (even-index) notes unchanged", () => {
    expect(swingOffset(0, 0.5, 1)).toBe(0);
    expect(swingOffset(1.0, 0.5, 1)).toBe(0);
  });

  it("delays off-beat (odd-index) notes", () => {
    expect(swingOffset(0.5, 0.5, 1)).toBeCloseTo(0.5 / 3, 6);
    expect(swingOffset(1.5, 0.5, 0.5)).toBeCloseTo(0.5 * (0.5 / 3), 6);
  });

  it("clamps swing to [0,1]", () => {
    expect(swingOffset(0.5, 0.5, 2)).toBeCloseTo(0.5 / 3, 6);
    expect(swingOffset(0.5, 0.5, -1)).toBe(0);
  });
});

describe("quantizeWithGroove", () => {
  it("matches plain quantize when swing is 0", () => {
    expect(quantizeWithGroove(0.6, 2, 1, 0)).toBeCloseTo(0.5, 6);
  });

  it("applies partial strength", () => {
    expect(quantizeWithGroove(0.6, 2, 0.5, 0)).toBeCloseTo(0.55, 6);
  });

  it("swings the off-beat after quantizing", () => {
    const expected = 0.5 + 0.5 / 3;
    expect(quantizeWithGroove(0.6, 2, 1, 1)).toBeCloseTo(expected, 6);
  });

  it("does not swing a downbeat", () => {
    expect(quantizeWithGroove(0.9, 2, 1, 1)).toBeCloseTo(1.0, 6);
  });

  it("exposes built-in templates with increasing swing", () => {
    expect(GROOVE_TEMPLATES[0].swing).toBe(0);
    for (let i = 1; i < GROOVE_TEMPLATES.length; i++) {
      expect(GROOVE_TEMPLATES[i].swing).toBeGreaterThan(GROOVE_TEMPLATES[i - 1].swing);
    }
  });
});
