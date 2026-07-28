import { describe, it, expect } from "vitest";
import { colorStr, hexToRgba } from "./theme";

describe("colorStr", () => {
  it("formats a packed 0xRRGGBB integer as a CSS hex string", () => {
    expect(colorStr(0x5b9bd5)).toBe("#5b9bd5");
    expect(colorStr(0x000000)).toBe("#000000");
  });

  it("masks off the alpha byte of an ARGB value", () => {
    expect(colorStr(0xff5b9bd5)).toBe("#5b9bd5");
  });
});

describe("hexToRgba", () => {
  it("converts a #rrggbb hex + alpha to an rgba() string", () => {
    expect(hexToRgba("#5b9bd5", 0.5)).toBe("rgba(91, 155, 213, 0.5)");
    expect(hexToRgba("#ffffff", 1)).toBe("rgba(255, 255, 255, 1)");
  });

  it("accepts a hex without the leading #", () => {
    expect(hexToRgba("ed7d31", 0.25)).toBe("rgba(237, 125, 49, 0.25)");
  });

  it("falls back to a neutral grey for malformed input", () => {
    expect(hexToRgba("nope", 0.5)).toBe("rgba(140, 140, 150, 0.5)");
    expect(hexToRgba("#12345", 0.5)).toBe("rgba(140, 140, 150, 0.5)");
  });
});
