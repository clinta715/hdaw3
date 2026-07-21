import { useRef, useEffect, useState, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { AutomationPointSnapshot } from "../rpc/types";
import { useAutomationStore } from "../store/automationStore";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "./snapUtils";
import { theme } from "../theme";
import "./AutomationLaneCanvas.css";

interface Props {
  laneName: string;
  points: AutomationPointSnapshot[];
  trackIndex: number;
  rpc: RpcClient;
  viewStartBeat: number;
  viewEndBeat: number;
  color?: string;
}

const POINT_RADIUS = 3;
const HIT_RADIUS = 6;

function buildPath(pts: AutomationPointSnapshot[], vw: number, vh: number, viewStart: number, viewEnd: number): string {
  if (pts.length === 0) return "";
  const range = viewEnd - viewStart;
  if (range <= 0) return "";
  const visible = pts.filter((p) => p.time >= viewStart && p.time <= viewEnd).sort((a, b) => a.time - b.time);
  if (visible.length === 0) return "";
  return visible.map((p, i) => {
    const x = ((p.time - viewStart) / range) * vw;
    const y = (1 - p.value) * vh;
    return `${i === 0 ? "M" : "L"}${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
}

function beatFromX(mx: number, cw: number, viewStart: number, viewEnd: number): number {
  return Math.max(viewStart, Math.min(viewEnd, viewStart + (mx / cw) * (viewEnd - viewStart)));
}

function valueFromY(my: number, ch: number): number {
  return Math.max(0, Math.min(1, 1 - my / ch));
}

function distToPoint(mx: number, my: number, px: number, py: number): number {
  const dx = mx - px;
  const dy = my - py;
  return Math.sqrt(dx * dx + dy * dy);
}

export default function AutomationLaneCanvas({
  laneName,
  points,
  trackIndex,
  rpc,
  viewStartBeat = 0,
  viewEndBeat = 32,
  color = theme.automationLine,
}: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ w: 600, h: 80 });
  const [hoveredTime, setHoveredTime] = useState<number | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const isDraggingRef = useRef(false);
  const [dragOrigTime, setDragOrigTime] = useState(0);
  const [dragOrigValue, setDragOrigValue] = useState(0);
  const [dragCurrentTime, setDragCurrentTime] = useState(0);
  const [dragCurrentValue, setDragCurrentValue] = useState(0);
  const dragOriginsRef = useRef<Map<number, { time: number; value: number }>>(new Map());
  const canvasBoundsRef = useRef<DOMRect | null>(null);

  // Context menu state
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; hitTime: number } | null>(null);

  const store = useAutomationStore();
  const laneSel = store.selectedPointTimes.get(laneName) ?? new Set<number>();

  // ResizeObserver for HiDPI
  useEffect(() => {
    const el = canvasRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      const dpr = window.devicePixelRatio || 1;
      el.width = width * dpr;
      el.height = height * dpr;
      el.style.width = `${width}px`;
      el.style.height = `${height}px`;
      setSize({ w: width, h: height });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Draw
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size.w, size.h);

    const sorted = [...points].filter(p => p.time >= viewStartBeat && p.time <= viewEndBeat).sort((a, b) => a.time - b.time);
    const range = viewEndBeat - viewStartBeat;
    if (range <= 0) return;

    // Fill under curve
    if (sorted.length >= 2) {
      ctx.beginPath();
      ctx.moveTo(0, size.h);
      sorted.forEach((p) => {
        const x = ((p.time - viewStartBeat) / range) * size.w;
        const y = (1 - p.value) * size.h;
        ctx.lineTo(x, y);
      });
      ctx.lineTo(((sorted[sorted.length - 1].time - viewStartBeat) / range) * size.w, size.h);
      ctx.closePath();
      ctx.fillStyle = color;
      ctx.globalAlpha = 0.15;
      ctx.fill();
      ctx.globalAlpha = 1;
    }

    // Line
    const linePath = buildPath(sorted, size.w, size.h, viewStartBeat, viewEndBeat);
    if (linePath) {
      const segments = new Path2D(linePath);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.stroke(segments);
    }

    // Points
    sorted.forEach((p) => {
      const x = ((p.time - viewStartBeat) / range) * size.w;
      const y = (1 - p.value) * size.h;
      const isSelected = laneSel.has(p.time);
      const isHovered = hoveredTime === p.time;

      // Selection ring
      if (isSelected) {
        ctx.beginPath();
        ctx.arc(x, y, POINT_RADIUS + 3, 0, Math.PI * 2);
        ctx.strokeStyle = "#fff";
        ctx.lineWidth = 2;
        ctx.stroke();
      }

      ctx.beginPath();
      ctx.arc(x, y, isHovered ? POINT_RADIUS + 1 : POINT_RADIUS, 0, Math.PI * 2);
      ctx.fillStyle = isSelected ? "#fff" : color;
      ctx.fill();
    });

    // Drag preview
    if (isDragging) {
      const dx = ((dragCurrentTime - viewStartBeat) / range) * size.w;
      const dy = (1 - dragCurrentValue) * size.h;

      // Dashed line from original to current
      const ox = ((dragOrigTime - viewStartBeat) / range) * size.w;
      const oy = (1 - dragOrigValue) * size.h;

      ctx.beginPath();
      ctx.moveTo(ox, oy);
      ctx.lineTo(dx, dy);
      ctx.strokeStyle = "#fff";
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.stroke();
      ctx.setLineDash([]);

      // Preview point
      ctx.beginPath();
      ctx.arc(dx, dy, POINT_RADIUS + 2, 0, Math.PI * 2);
      ctx.fillStyle = "#fff";
      ctx.globalAlpha = 0.6;
      ctx.fill();
      ctx.globalAlpha = 1;
    }
  }, [points, size, laneSel, hoveredTime, isDragging, dragCurrentTime, dragCurrentValue, dragOrigTime, dragOrigValue, color, viewStartBeat, viewEndBeat]);

  const getPointAt = useCallback((mx: number, my: number): number | null => {
    const range = viewEndBeat - viewStartBeat;
    if (range <= 0) return null;
    const sorted = [...points].filter(p => p.time >= viewStartBeat && p.time <= viewEndBeat);
    for (const p of sorted) {
      const px = ((p.time - viewStartBeat) / range) * size.w;
      const py = (1 - p.value) * size.h;
      if (distToPoint(mx, my, px, py) < HIT_RADIUS) {
        return p.time;
      }
    }
    return null;
  }, [points, size, viewStartBeat, viewEndBeat]);

  const commitMove = useCallback(async () => {
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const rawDeltaTime = dragCurrentTime - dragOrigTime;
    const snappedDeltaTime = snapEnabled ? snapToGrid(rawDeltaTime, snapDivision) : rawDeltaTime;
    const deltaValue = dragCurrentValue - dragOrigValue;
    if (Math.abs(snappedDeltaTime) < 0.001 && Math.abs(deltaValue) < 0.001) return;

    const origins = dragOriginsRef.current;
    for (const [, orig] of origins) {
      const newTime = Math.max(0, orig.time + snappedDeltaTime);
      const newValue = Math.max(0, Math.min(1, orig.value + deltaValue));
      const needsTimeChange = Math.abs(newTime - orig.time) > 0.001;
      if (needsTimeChange) {
        await rpc.call("project.removeAutomationPoint", { trackIndex, lane: laneName, time: orig.time });
        await rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time: newTime, value: newValue });
      } else {
        await rpc.call("project.setAutomationPointValue", { trackIndex, lane: laneName, time: orig.time, value: newValue });
      }
    }
    await store.fetchForTrack(trackIndex, rpc);
    store.clearSelection(laneName);
  }, [dragOrigTime, dragOrigValue, dragCurrentTime, dragCurrentValue, trackIndex, laneName, rpc, store]);

  // Keep the latest commitMove in a ref so the window-level mouseup listener
  // (registered once when the drag starts) can call the current version
  // without re-subscribing on every render. Without this, releasing the mouse
  // *outside* the canvas left the window handler with a stale/empty closure
  // and the drag move was never committed to the engine.
  const commitMoveRef = useRef(commitMove);
  commitMoveRef.current = commitMove;

  const addPointAt = useCallback(async (mx: number, my: number) => {
    const rawTime = beatFromX(mx, size.w, viewStartBeat, viewEndBeat);
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const t = snapEnabled ? snapToGrid(rawTime, snapDivision) : rawTime;
    const v = valueFromY(my, size.h);
    await store.addPoint(trackIndex, laneName, t, v, rpc);
  }, [size, viewStartBeat, viewEndBeat, trackIndex, laneName, rpc, store]);

  const handleMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (e.button !== 0) return;
    setContextMenu(null);
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    canvasBoundsRef.current = rect;
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    const hitTime = getPointAt(mx, my);
    if (hitTime !== null) {
      if (e.ctrlKey) {
        store.selectPoint(laneName, hitTime, false, true);
        return;
      }
      if (e.shiftKey) {
        store.selectPoint(laneName, hitTime, true, false);
        return;
      }
      if (!laneSel.has(hitTime)) {
        store.selectPoint(laneName, hitTime, false, false);
      }
      const currentSel = useAutomationStore.getState().selectedPointTimes.get(laneName) ?? new Set<number>();
      const origins = new Map<number, { time: number; value: number }>();
      for (const t of currentSel) {
        const pt = points.find((p) => p.time === t);
        if (pt) origins.set(t, { time: pt.time, value: pt.value });
      }
      if (!origins.has(hitTime)) {
        origins.set(hitTime, { time: hitTime, value: points.find((p) => p.time === hitTime)?.value ?? 0 });
      }
      dragOriginsRef.current = origins;
      isDraggingRef.current = true;
      setIsDragging(true);
      setDragOrigTime(hitTime);
      setDragOrigValue(points.find((p) => p.time === hitTime)?.value ?? 0);
      setDragCurrentTime(hitTime);
      setDragCurrentValue(points.find((p) => p.time === hitTime)?.value ?? 0);
    } else {
      if (!e.shiftKey && !e.ctrlKey) {
        store.clearSelection(laneName);
        addPointAt(mx, my);
      } else {
        store.clearSelection(laneName);
      }
    }
  }, [getPointAt, laneName, points, store, addPointAt, laneSel]);

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    if (isDragging) {
      const t = beatFromX(mx, size.w, viewStartBeat, viewEndBeat);
      const v = valueFromY(my, size.h);
      setDragCurrentTime(t);
      setDragCurrentValue(v);
    } else {
      const hitTime = getPointAt(mx, my);
      setHoveredTime(hitTime);
      const el = canvasRef.current;
      if (el) {
        el.classList.toggle("alc-hover-point", hitTime !== null);
        el.classList.toggle("alc-dragging", false);
      }
    }
  }, [isDragging, getPointAt, size, viewStartBeat, viewEndBeat]);

  const handleMouseUp = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    // Commit is handled by the single window-level mouseup listener (see the
    // isDragging effect below), which fires for releases inside AND outside
    // the canvas. This in-canvas handler just refreshes hover state.
    if (isDragging) {
      const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const hitTime = getPointAt(mx, my);
      setHoveredTime(hitTime);
    }
  }, [isDragging, getPointAt]);

  // Window-level mouse handlers for drag-outside-canvas support
  useEffect(() => {
    if (!isDragging) return;

    const handleWindowMouseMove = (e: MouseEvent) => {
      if (!isDraggingRef.current) return;
      const rect = canvasBoundsRef.current;
      if (!rect) return;
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const t = beatFromX(mx, size.w, viewStartBeat, viewEndBeat);
      const v = valueFromY(my, size.h);
      setDragCurrentTime(t);
      setDragCurrentValue(v);
    };

    const handleWindowMouseUp = async () => {
      if (!isDraggingRef.current) return;
      isDraggingRef.current = false;
      setIsDragging(false);
      // Commit the drag move via the ref so we always call the latest
      // commitMove (which closes over the freshest dragOrig*/dragCurrent*
      // state). Previously this body was empty, so releasing outside the
      // canvas dropped the move entirely and points snapped back.
      try {
        await commitMoveRef.current();
      } catch (err) {
        console.error("automation drag commit failed:", err);
      }
    };

    window.addEventListener("mousemove", handleWindowMouseMove);
    window.addEventListener("mouseup", handleWindowMouseUp);
    return () => {
      window.removeEventListener("mousemove", handleWindowMouseMove);
      window.removeEventListener("mouseup", handleWindowMouseUp);
    };
  }, [isDragging, size, viewStartBeat, viewEndBeat]);

  const handleContextMenu = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const hitTime = getPointAt(mx, my);
    if (hitTime !== null) {
      if (!laneSel.has(hitTime)) {
        store.selectPoint(laneName, hitTime, false, false);
      }
      setContextMenu({ x: e.clientX, y: e.clientY, hitTime });
    }
  }, [getPointAt, laneSel, laneName, store]);

  // Close context menu on outside click
  useEffect(() => {
    if (!contextMenu) return;
    const close = (e: MouseEvent) => {
      const target = e.target as HTMLElement;
      if (!target.closest('.alc-context-menu')) {
        setContextMenu(null);
      }
    };
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, [contextMenu]);

  return (
    <div className="automation-lane-canvas">
      <div className="alc-header">{laneName}</div>
      <canvas
        ref={canvasRef}
        className="alc-canvas"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={() => setHoveredTime(null)}
        onContextMenu={handleContextMenu}
      />
      {contextMenu && (
        <div
          className="alc-context-menu"
          style={{ left: contextMenu.x, top: contextMenu.y }}
          onMouseDown={(e) => e.stopPropagation()}
        >
          <button onMouseDown={(e) => {
            e.stopPropagation();
            const timesToDelete = laneSel.has(contextMenu.hitTime) ? [...laneSel] : [contextMenu.hitTime];
            store.removePoints(trackIndex, laneName, timesToDelete, rpc);
            setContextMenu(null);
          }}>
            Delete Point{laneSel.has(contextMenu.hitTime) && laneSel.size > 1 ? `s (${laneSel.size})` : ""}
          </button>
          <button onMouseDown={(e) => {
            e.stopPropagation();
            store.clearSelection(laneName);
            setContextMenu(null);
          }}>
            Clear Selection
          </button>
        </div>
      )}
    </div>
  );
}
