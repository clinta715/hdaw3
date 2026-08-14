import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import {
  useTimelineZoom,
  DEFAULT_PPS,
  MIN_PPS,
  MAX_PPS,
} from "./useTimelineZoom";

// Build a fake scrollable element with a deterministic viewport rect. jsdom
// returns zeros from getBoundingClientRect and ignores scrollLeft assignment
// (it's a plain prop, not a layout-derived value), so we control both.
const makeEl = (opts: { left?: number; width?: number; scrollLeft?: number } = {}) => {
  const el = document.createElement("div");
  const left = opts.left ?? 0;
  const width = opts.width ?? 1000;
  el.getBoundingClientRect = () => ({
    left,
    top: 0,
    width,
    height: 400,
    right: left + width,
    bottom: 400,
    x: left,
    y: 0,
    toJSON: () => {},
  });
  el.scrollLeft = opts.scrollLeft ?? 0;
  // clientWidth is read by zoomFit; default to width for consistency.
  Object.defineProperty(el, "clientWidth", { value: width, configurable: true });
  return el;
};

const makeRef = (el: HTMLDivElement | null) => ({ current: el });

const renderZoom = (overrides: {
  left?: number;
  width?: number;
  scrollLeft?: number;
  maxEnd?: number;
  initialPps?: number;
} = {}) => {
  const bodyEl = makeEl({ left: 0, width: overrides.width ?? 1000 });
  const tracksEl = makeEl({
    left: overrides.left ?? 0,
    width: overrides.width ?? 1000,
    scrollLeft: overrides.scrollLeft ?? 0,
  });
  const rulerEl = makeEl({ left: overrides.left ?? 0, width: overrides.width ?? 1000 });
  // Mirror the real DOM nesting (body > ruler, body > tracks) so wheel events
  // dispatched on a ruler descendant bubble to the body-anchored listener and
  // rulerRef.contains(target) resolves true.
  bodyEl.appendChild(rulerEl);
  bodyEl.appendChild(tracksEl);
  document.body.appendChild(bodyEl);
  const bodyRef = makeRef(bodyEl);
  const tracksRef = makeRef(tracksEl);
  const rulerRef = makeRef(rulerEl);

  const { result } = renderHook(() =>
    useTimelineZoom({
      maxEnd: overrides.maxEnd ?? 32,
      bodyRef,
      tracksRef,
      rulerRef,
    }),
  );

  if (overrides.initialPps != null && overrides.initialPps !== DEFAULT_PPS) {
    act(() => result.current.setPps(overrides.initialPps!));
  }

  return { result, bodyEl, tracksEl, rulerEl, bodyRef, tracksRef, rulerRef };
};

const fireWheel = (
  el: HTMLDivElement,
  opts: { clientX?: number; deltaY?: number; ctrlKey?: boolean; shiftKey?: boolean; altKey?: boolean; metaKey?: boolean } = {},
) => {
  const event = new WheelEvent("wheel", {
    clientX: opts.clientX ?? 0,
    deltaY: opts.deltaY ?? -100,
    ctrlKey: opts.ctrlKey ?? false,
    shiftKey: opts.shiftKey ?? false,
    altKey: opts.altKey ?? false,
    metaKey: opts.metaKey ?? false,
    bubbles: true,
    cancelable: true,
  });
  el.dispatchEvent(event);
};

describe("useTimelineZoom", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  describe("Ctrl+wheel pointer-centered zoom (G2)", () => {
    it("pins the beat under the cursor (zoom in at mid-viewport)", () => {
      const { result, bodyEl, tracksEl } = renderZoom({ width: 1000 });

      const cursorX = 500;
      const beforePps = result.current.pps;
      const beforeScroll = tracksEl.scrollLeft;
      const cursorOffset = cursorX; // rect.left = 0
      const beatAtCursor = (cursorOffset + beforeScroll) / beforePps;

      act(() => fireWheel(bodyEl, { clientX: cursorX, deltaY: -100, ctrlKey: true }));

      const afterPps = result.current.pps;
      expect(afterPps).toBeGreaterThan(beforePps);

      const expectedScroll = Math.max(0, beatAtCursor * afterPps - cursorOffset);
      expect(tracksEl.scrollLeft).toBeCloseTo(expectedScroll, 5);

      // The beat under the cursor is preserved by construction.
      const beatAfter = (cursorOffset + tracksEl.scrollLeft) / afterPps;
      expect(beatAfter).toBeCloseTo(beatAtCursor, 6);
    });

    it("pins the beat under the cursor when scrolled and zooming out", () => {
      const { result, bodyEl, tracksEl } = renderZoom({
        width: 1000,
        scrollLeft: 200,
        initialPps: 50,
      });

      const cursorX = 700;
      const beforePps = result.current.pps;
      const beforeScroll = tracksEl.scrollLeft;
      const cursorOffset = cursorX;
      const beatAtCursor = (cursorOffset + beforeScroll) / beforePps;

      act(() => fireWheel(bodyEl, { clientX: cursorX, deltaY: 100, ctrlKey: true }));

      const afterPps = result.current.pps;
      expect(afterPps).toBeLessThan(beforePps);

      const expectedScroll = Math.max(0, beatAtCursor * afterPps - cursorOffset);
      expect(tracksEl.scrollLeft).toBeCloseTo(expectedScroll, 5);

      const beatAfter = (cursorOffset + tracksEl.scrollLeft) / afterPps;
      expect(beatAfter).toBeCloseTo(beatAtCursor, 6);
    });

    it("syncs rulerRef.scrollLeft to match tracks", () => {
      const { bodyEl, tracksEl, rulerEl } = renderZoom({ width: 1000 });
      act(() => fireWheel(bodyEl, { clientX: 400, deltaY: -100, ctrlKey: true }));
      expect(rulerEl.scrollLeft).toBe(tracksEl.scrollLeft);
      expect(rulerEl.scrollLeft).toBeGreaterThan(0);
    });
  });

  describe("plain wheel inside ruler zooms (G1)", () => {
    it("zooms when the wheel target is inside the ruler (no modifier)", () => {
      // Attach a child to rulerEl so we can dispatch from a Node contained by it.
      const { result, rulerEl } = renderZoom({ width: 1000 });
      const target = document.createElement("div");
      rulerEl.appendChild(target);
      const beforePps = result.current.pps;
      act(() => fireWheel(target, { clientX: 300, deltaY: -100 }));
      expect(result.current.pps).toBeGreaterThan(beforePps);
    });
  });

  describe("native scroll regressions (G5, G6)", () => {
    it("does NOT zoom on plain wheel over the tracks body (falls through)", () => {
      const { result, bodyEl } = renderZoom({ width: 1000 });
      const beforePps = result.current.pps;
      act(() => fireWheel(bodyEl, { clientX: 300, deltaY: -100 }));
      expect(result.current.pps).toBe(beforePps);
    });

    it("does NOT zoom on Shift+wheel (horizontal scroll passthrough)", () => {
      // Tracks body: Shift+wheel must pass through regardless of target.
      const { result: r1, bodyEl } = renderZoom({ width: 1000 });
      const before1 = r1.current.pps;
      act(() => fireWheel(bodyEl, { clientX: 300, deltaY: -100, shiftKey: true }));
      expect(r1.current.pps).toBe(before1);

      // Ruler: even though plain wheel zooms there, Shift-only stays a passthrough.
      const { result: r2, rulerEl } = renderZoom({ width: 1000 });
      const rulerChild = document.createElement("div");
      rulerEl.appendChild(rulerChild);
      const before2 = r2.current.pps;
      act(() => fireWheel(rulerChild, { clientX: 300, deltaY: -100, shiftKey: true }));
      expect(r2.current.pps).toBe(before2);
    });
  });

  describe("clamp + button zoom", () => {
    it("zoomIn / zoomOut multiply by 1.25 / 0.8", () => {
      const { result } = renderZoom({ width: 1000 });
      const start = result.current.pps;
      act(() => result.current.zoomIn());
      expect(result.current.pps).toBeCloseTo(start * 1.25, 5);
      act(() => result.current.zoomOut());
      expect(result.current.pps).toBeCloseTo(start, 5);
    });

    it("clamps to MAX_PPS / MIN_PPS", () => {
      const { result } = renderZoom({ width: 1000 });
      act(() => result.current.setPps(MAX_PPS));
      act(() => result.current.zoomIn());
      expect(result.current.pps).toBe(MAX_PPS);
      act(() => result.current.setPps(MIN_PPS));
      act(() => result.current.zoomOut());
      expect(result.current.pps).toBe(MIN_PPS);
    });
  });

  describe("zoomToRange (marquee helper)", () => {
    it("sets pps = viewportWidth / (b2 - b1) and scrolls to b1", () => {
      const { result, tracksEl } = renderZoom({ width: 1000, initialPps: 40 });
      // Drag covers beats 10..20 → 10 beats → pps = 1000/10 = 100.
      act(() => result.current.zoomToRange(10, 20, 1000));
      expect(result.current.pps).toBe(100);
      expect(tracksEl.scrollLeft).toBeCloseTo(10 * 100, 5);
    });

    it("swaps reversed ranges (right-to-left drag)", () => {
      const { result, tracksEl } = renderZoom({ width: 1000, initialPps: 40 });
      act(() => result.current.zoomToRange(20, 10, 1000));
      expect(result.current.pps).toBe(100);
      expect(tracksEl.scrollLeft).toBeCloseTo(10 * 100, 5);
    });

    it("clamps the computed pps to MIN_PPS / MAX_PPS", () => {
      const { result } = renderZoom({ width: 1000 });
      // Tiny range → would exceed MAX_PPS.
      act(() => result.current.zoomToRange(0, 0.01, 1000));
      expect(result.current.pps).toBe(MAX_PPS);
      // Huge range → would fall below MIN_PPS.
      act(() => result.current.zoomToRange(0, 1e6, 1000));
      expect(result.current.pps).toBe(MIN_PPS);
    });

    it("is a no-op for non-positive range or viewport", () => {
      const { result } = renderZoom({ width: 1000 });
      const before = result.current.pps;
      act(() => result.current.zoomToRange(5, 5, 1000));
      expect(result.current.pps).toBe(before);
      act(() => result.current.zoomToRange(5, 10, 0));
      expect(result.current.pps).toBe(before);
    });
  });
});
