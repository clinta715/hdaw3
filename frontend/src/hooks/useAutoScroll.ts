import { useRef, useCallback, useEffect, type RefObject } from "react";

const EDGE_ZONE = 40;
const MIN_SPEED = 2;
const MAX_SPEED = 20;

function computeSpeed(distanceFromEdge: number): number {
  const d = Math.max(0, distanceFromEdge);
  if (d >= EDGE_ZONE) return 0;
  const pastEdge = EDGE_ZONE - d;
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
