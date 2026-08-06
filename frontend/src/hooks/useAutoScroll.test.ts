import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useAutoScroll } from "./useAutoScroll";

function makeContainer(overrides?: Partial<DOMRect>): {
  ref: React.RefObject<HTMLDivElement | null>;
  el: HTMLDivElement;
} {
  const el = document.createElement("div");
  const defaults = { left: 0, top: 0, right: 800, bottom: 600, width: 800, height: 600, x: 0, y: 0, toJSON: () => {} };
  el.getBoundingClientRect = () => ({ ...defaults, ...overrides } as DOMRect);
  Object.defineProperty(el, "scrollWidth", { value: 2000, configurable: true });
  Object.defineProperty(el, "clientWidth", { value: 800, configurable: true });
  Object.defineProperty(el, "scrollHeight", { value: 1500, configurable: true });
  Object.defineProperty(el, "clientHeight", { value: 600, configurable: true });
  el.scrollLeft = 0;
  el.scrollTop = 0;
  return { ref: { current: el }, el };
}

describe("useAutoScroll", () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it("does not scroll when cursor is in the center", () => {
    const { ref, el } = makeContainer();
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      result.current.update(400, 300);
      vi.advanceTimersByTime(100);
    });

    expect(el.scrollLeft).toBe(0);
    expect(el.scrollTop).toBe(0);
  });

  it("scrolls right when cursor is near the right edge", () => {
    const { ref, el } = makeContainer();
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      // 10px from right edge (800 - 10 = 790)
      result.current.update(790, 300);
      vi.advanceTimersByTime(50);
    });

    expect(el.scrollLeft).toBeGreaterThan(0);
  });

  it("scrolls left when cursor is near the left edge", () => {
    const { ref, el } = makeContainer();
    el.scrollLeft = 100;
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      // 10px from left edge
      result.current.update(10, 300);
      vi.advanceTimersByTime(50);
    });

    expect(el.scrollLeft).toBeLessThan(100);
  });

  it("scrolls down when cursor is near the bottom edge", () => {
    const { ref, el } = makeContainer();
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      // 10px from bottom edge
      result.current.update(400, 590);
      vi.advanceTimersByTime(50);
    });

    expect(el.scrollTop).toBeGreaterThan(0);
  });

  it("scrolls up when cursor is near the top edge", () => {
    const { ref, el } = makeContainer();
    el.scrollTop = 100;
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      // 10px from top edge
      result.current.update(400, 10);
      vi.advanceTimersByTime(50);
    });

    expect(el.scrollTop).toBeLessThan(100);
  });

  it("stops scrolling when stop() is called", () => {
    const { ref, el } = makeContainer();
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      result.current.update(790, 300);
      vi.advanceTimersByTime(20);
    });
    const scrollAfterStart = el.scrollLeft;
    expect(scrollAfterStart).toBeGreaterThan(0);

    act(() => {
      result.current.stop();
      vi.advanceTimersByTime(100);
    });

    expect(el.scrollLeft).toBe(scrollAfterStart);
  });

  it("clamps scroll to bounds", () => {
    const { ref, el } = makeContainer();
    el.scrollLeft = 1190; // close to max (2000 - 800 = 1200)
    const { result } = renderHook(() => useAutoScroll(ref));

    act(() => {
      result.current.update(790, 300);
      vi.advanceTimersByTime(200);
    });

    expect(el.scrollLeft).toBeLessThanOrEqual(1200);
  });

  it("accelerates speed with distance past edge", () => {
    const { ref: ref1, el: el1 } = makeContainer();
    const { ref: ref2, el: el2 } = makeContainer();

    const { result: result1 } = renderHook(() => useAutoScroll(ref1));
    const { result: result2 } = renderHook(() => useAutoScroll(ref2));

    // 5px from edge (closer)
    act(() => {
      result1.current.update(795, 300);
      vi.advanceTimersByTime(50);
    });

    // 30px from edge (further from edge -> slower)
    act(() => {
      result2.current.update(770, 300);
      vi.advanceTimersByTime(50);
    });

    // Closer to edge = faster scroll
    expect(el1.scrollLeft).toBeGreaterThan(el2.scrollLeft);
  });
});
