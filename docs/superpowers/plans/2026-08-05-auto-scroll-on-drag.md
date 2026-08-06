# Auto-Scroll on Drag — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add auto-scroll when dragging clips or notes near the edges of their scroll containers, so the view scrolls automatically and the user can place objects beyond the visible area.

**Architecture:** A shared `useAutoScroll` hook uses `requestAnimationFrame` to programmatically scroll a container when the cursor is within 40px of any edge. Speed accelerates linearly from 2 px/frame at the edge to 20 px/frame at 40px past the edge. Integrated into `useTimelineDrag` (clip drag) and `NoteGrid` (note drag/resize).

**Tech Stack:** React hooks, `requestAnimationFrame`, Vitest + `@testing-library/react`

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `frontend/src/hooks/useAutoScroll.ts` | **Create** | Shared auto-scroll hook |
| `frontend/src/hooks/useAutoScroll.test.ts` | **Create** | Unit tests |
| `frontend/src/hooks/useTimelineDrag.ts` | **Modify** | Integrate auto-scroll into clip drag |
| `frontend/src/components/NoteGrid.tsx` | **Modify** | Integrate auto-scroll into note drag/resize |

---

### Task 1: Create `useAutoScroll` hook

**Files:**
- Create: `frontend/src/hooks/useAutoScroll.ts`

- [ ] **Step 1: Create the hook file**

```typescript
import { useRef, useCallback, useEffect, type RefObject } from "react";

const EDGE_ZONE = 40;
const MIN_SPEED = 2;
const MAX_SPEED = 20;

function computeSpeed(distanceFromEdge: number): number {
  if (distanceFromEdge >= EDGE_ZONE) return 0;
  const pastEdge = EDGE_ZONE - distanceFromEdge;
  return MIN_SPEED + (MAX_SPEED - MIN_SPEED) * (pastEdge / EDGE_ZONE);
}

export function useAutoScroll(containerRef: RefObject<HTMLElement | null>) {
  const rafRef = useRef<number | null>(null);
  const speedRef = useRef({ vx: 0, vy: 0 });

  const tick = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;

    const { vx, vy } = speedRef.current;
    if (vx === 0 && vy === 0) {
      rafRef.current = null;
      return;
    }

    const maxScrollLeft = el.scrollWidth - el.clientWidth;
    const maxScrollTop = el.scrollHeight - el.clientHeight;
    el.scrollLeft = Math.min(maxScrollLeft, Math.max(0, el.scrollLeft + vx));
    el.scrollTop = Math.min(maxScrollTop, Math.max(0, el.scrollTop + vy));

    rafRef.current = requestAnimationFrame(tick);
  }, [containerRef]);

  const startLoop = useCallback(() => {
    if (rafRef.current == null) {
      rafRef.current = requestAnimationFrame(tick);
    }
  }, [tick]);

  const update = useCallback(
    (clientX: number, clientY: number) => {
      const el = containerRef.current;
      if (!el) return;

      const rect = el.getBoundingClientRect();
      const distLeft = clientX - rect.left;
      const distRight = rect.right - clientX;
      const distTop = clientY - rect.top;
      const distBottom = rect.bottom - clientY;

      let vx = 0;
      let vy = 0;

      if (distLeft < EDGE_ZONE) {
        vx = -computeSpeed(distLeft);
      } else if (distRight < EDGE_ZONE) {
        vx = computeSpeed(distRight);
      }

      if (distTop < EDGE_ZONE) {
        vy = -computeSpeed(distTop);
      } else if (distBottom < EDGE_ZONE) {
        vy = computeSpeed(distBottom);
      }

      speedRef.current = { vx, vy };

      if (vx !== 0 || vy !== 0) {
        startLoop();
      }
    },
    [containerRef, startLoop]
  );

  const stop = useCallback(() => {
    speedRef.current = { vx: 0, vy: 0 };
    if (rafRef.current != null) {
      cancelAnimationFrame(rafRef.current);
      rafRef.current = null;
    }
  }, []);

  useEffect(() => {
    return () => {
      if (rafRef.current != null) {
        cancelAnimationFrame(rafRef.current);
        rafRef.current = null;
      }
    };
  }, []);

  return { update, stop };
}
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit --pretty 2>&1 | head -20`
Expected: No errors from `useAutoScroll.ts`

---

### Task 2: Write tests for `useAutoScroll`

**Files:**
- Create: `frontend/src/hooks/useAutoScroll.test.ts`

- [ ] **Step 1: Create the test file**

```typescript
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

    // 30px from edge (further into the zone → faster)
    act(() => {
      result2.current.update(770, 300);
      vi.advanceTimersByTime(50);
    });

    expect(el2.scrollLeft).toBeGreaterThan(el1.scrollLeft);
  });
});
```

- [ ] **Step 2: Run the tests**

Run: `cd frontend && npx vitest run src/hooks/useAutoScroll.test.ts`
Expected: All tests pass

---

### Task 3: Integrate auto-scroll into timeline clip drag

**Files:**
- Modify: `frontend/src/hooks/useTimelineDrag.ts`

- [ ] **Step 1: Import the hook**

At line 1 of `useTimelineDrag.ts`, add to the existing imports:

```typescript
import { useAutoScroll } from "./useAutoScroll";
```

- [ ] **Step 2: Instantiate the hook inside `useTimelineDrag`**

After line 63 (after the destructuring of params), add:

```typescript
const autoScroll = useAutoScroll(tracksRef);
```

- [ ] **Step 3: Call `autoScroll.update` in the `onMove` handler**

In the `onMove` function inside `handleClipMouseDown` (around line 105), add the auto-scroll update call. The modified `onMove` should be:

```typescript
const onMove = (ev: globalThis.MouseEvent) => {
  if (!engaged) {
    if (engagementRef.current === "rubber") {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      return;
    }
    const dx = ev.clientX - startClientX;
    const dy = ev.clientY - startClientY;
    if (dx * dx + dy * dy < 16) return;
    engaged = true;
    engagementRef.current = "clip";
    useUiStore.getState().setStatusHint("Drag: move · Ctrl+drag: duplicate · Alt+drag: stretch · Shift: toggle snap");
    updateDrag(pendingDrag);
  }
  autoScroll.update(ev.clientX, ev.clientY);
  handleMouseMoveRef.current(ev);
};
```

- [ ] **Step 4: Call `autoScroll.stop` in the `onUp` handler**

In the `onUp` function inside `handleClipMouseDown` (around line 122), add stop. The modified `onUp` should be:

```typescript
const onUp = () => {
  window.removeEventListener("mousemove", onMove);
  window.removeEventListener("mouseup", onUp);
  engagementRef.current = "none";
  useUiStore.getState().setStatusHint(null);
  autoScroll.stop();
  if (engaged) {
    handleMouseUpRef.current();
  }
};
```

- [ ] **Step 5: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit --pretty 2>&1 | head -20`
Expected: No errors

- [ ] **Step 6: Run existing tests**

Run: `cd frontend && npx vitest run src/hooks/useTimelineDrag.test.ts`
Expected: All existing tests still pass

---

### Task 4: Integrate auto-scroll into piano roll note drag

**Files:**
- Modify: `frontend/src/components/NoteGrid.tsx`

- [ ] **Step 1: Import the hook**

At line 1 of `NoteGrid.tsx`, add to the existing imports:

```typescript
import { useAutoScroll } from "../hooks/useAutoScroll";
```

- [ ] **Step 2: Instantiate the hook inside the NoteGrid component**

After the `gridRef` declaration (line 107), add:

```typescript
const autoScroll = useAutoScroll(gridRef);
```

- [ ] **Step 3: Call `autoScroll.update` in the note drag `handleMouseMove`**

In the `handleMouseMove` callback (line 177), add the auto-scroll update at the beginning of the function body, before the `setResizeState` call:

```typescript
const handleMouseMove = useCallback((e: globalThis.MouseEvent) => {
  autoScroll.update(e.clientX, e.clientY);
  // ... rest of existing code unchanged
}, []);
```

- [ ] **Step 4: Call `autoScroll.stop` in `handleMouseUp`**

In the `handleMouseUp` callback (line 205), add `autoScroll.stop()` at the beginning:

```typescript
const handleMouseUp = useCallback(async () => {
  autoScroll.stop();
  // ... rest of existing code unchanged
}, [rpc, clipId]);
```

- [ ] **Step 5: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit --pretty 2>&1 | head -20`
Expected: No errors

- [ ] **Step 6: Run frontend unit tests**

Run: `cd frontend && npm test`
Expected: All tests pass

- [ ] **Step 7: Build frontend**

Run: `cd frontend && npm run build`
Expected: Build succeeds

---

### Task 5: Manual verification

- [ ] **Step 1: Build and launch**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 2: Test timeline auto-scroll**

1. Open HDAW, create a project with clips
2. Drag a clip toward the right edge → view should scroll right
3. Drag a clip toward the left edge → view should scroll left
4. Release mouse → scrolling stops
5. Drag clip near edge, hold still → view scrolls, clip follows on next mouse move

- [ ] **Step 3: Test piano roll auto-scroll**

1. Open a MIDI clip in the piano roll
2. Drag a note toward the right edge → view scrolls right
3. Drag a note toward the top/bottom edge → view scrolls vertically
4. Resize a note near the right edge → view scrolls right
5. Release → scrolling stops
