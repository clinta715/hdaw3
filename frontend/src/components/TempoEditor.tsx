import { useRef, useEffect, useState, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { TempoPointSnapshot } from "../rpc/types";
import { theme } from "../theme";
import "./TempoEditor.css";

interface Props {
  rpc: RpcClient;
}

const POINT_RADIUS = 4;
const HIT_RADIUS = 8;

function xFromTime(t: number, cw: number, vStart: number, vEnd: number): number {
  const range = vEnd - vStart;
  if (range <= 0) return 0;
  return ((t - vStart) / range) * cw;
}

function timeFromX(mx: number, cw: number, vStart: number, vEnd: number): number {
  const range = vEnd - vStart;
  if (cw <= 0) return vStart;
  return vStart + (mx / cw) * range;
}

function yFromBpm(bpm: number, ch: number, bpmMin: number, bpmMax: number): number {
  const range = bpmMax - bpmMin;
  if (range <= 0) return 0;
  return ch - ((bpm - bpmMin) / range) * ch;
}

function bpmFromY(my: number, ch: number, bpmMin: number, bpmMax: number): number {
  const range = bpmMax - bpmMin;
  if (ch <= 0) return bpmMin;
  return bpmMin + (1 - my / ch) * range;
}

function distToPoint(mx: number, my: number, px: number, py: number): number {
  return Math.sqrt((mx - px) ** 2 + (my - py) ** 2);
}

export default function TempoEditor({ rpc }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sizeRef = useRef({ w: 600, h: 200 });

  const [points, setPoints] = useState<TempoPointSnapshot[]>([]);
  const [selectedTime, setSelectedTime] = useState<number | null>(null);
  const [hoverTime, setHoverTime] = useState<number | null>(null);
  const [dragState, setDragState] = useState<{
    unsortedIndex: number;
    startTime: number;
    startBpm: number;
    offsetX: number;
    offsetY: number;
  } | null>(null);
  const [viewStart, setViewStart] = useState(0);
  const [viewEnd, setViewEnd] = useState(60);
  const [bpmMin, setBpmMin] = useState(20);
  const [bpmMax, setBpmMax] = useState(300);

  const drawRef = useRef<() => void>(() => {});

  // Keep a ref to current points for the mouseup handler (avoids stale closure)
  const usePointsRef = useRef(points);
  usePointsRef.current = points;

  const refresh = useCallback(() => {
    rpc
      .call("read.getTempoPoints")
      .then((pts) => {
        setPoints(pts as TempoPointSnapshot[]);
      })
      .catch(console.error);
  }, [rpc]);

  useEffect(() => {
    refresh();
  }, [refresh]);

  // ResizeObserver for HiDPI
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver((entries) => {
      const { width, height } = entries[0].contentRect;
      const dpr = window.devicePixelRatio || 1;
      canvas.width = width * dpr;
      canvas.height = height * dpr;
      canvas.style.width = `${width}px`;
      canvas.style.height = `${height}px`;
      sizeRef.current = { w: width, h: height };
      drawRef.current();
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, []);

  // Draw function
  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const { w: cw, h: ch } = sizeRef.current;
    ctx.clearRect(0, 0, cw, ch);

    // BPM grid lines (horizontal)
    ctx.strokeStyle = "rgba(255,255,255,0.06)";
    ctx.lineWidth = 1;
    const bpmStep = 20;
    const firstBpm = Math.ceil(bpmMin / bpmStep) * bpmStep;
    for (let bpm = firstBpm; bpm <= bpmMax; bpm += bpmStep) {
      const y = yFromBpm(bpm, ch, bpmMin, bpmMax);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(cw, y);
      ctx.stroke();
    }

    // BPM labels
    ctx.fillStyle = "rgba(255,255,255,0.25)";
    ctx.font = "10px system-ui";
    ctx.textAlign = "left";
    for (let bpm = firstBpm; bpm <= bpmMax; bpm += bpmStep) {
      const y = yFromBpm(bpm, ch, bpmMin, bpmMax);
      ctx.fillText(`${bpm}`, 4, y - 3);
    }

    // Time grid lines (vertical) — every 10 seconds
    const timeStep = 10;
    const firstTime = Math.ceil(viewStart / timeStep) * timeStep;
    for (let t = firstTime; t <= viewEnd; t += timeStep) {
      const x = xFromTime(t, cw, viewStart, viewEnd);
      ctx.strokeStyle = "rgba(255,255,255,0.06)";
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, ch);
      ctx.stroke();

      // Time label
      ctx.fillStyle = "rgba(255,255,255,0.25)";
      ctx.textAlign = "center";
      ctx.fillText(`${t}s`, x, ch - 4);
    }

    // Sort points by time
    const sorted = [...points].sort((a, b) => a.timeSeconds - b.timeSeconds);
    if (sorted.length === 0) return;

    // Step-function fill under curve
    ctx.beginPath();
    ctx.moveTo(xFromTime(sorted[0].timeSeconds, cw, viewStart, viewEnd), ch);
    for (const p of sorted) {
      const x = xFromTime(p.timeSeconds, cw, viewStart, viewEnd);
      const y = yFromBpm(p.bpm, ch, bpmMin, bpmMax);
      ctx.lineTo(x, y);
    }
    ctx.lineTo(xFromTime(sorted[sorted.length - 1].timeSeconds, cw, viewStart, viewEnd), ch);
    ctx.closePath();
    ctx.fillStyle = theme.accent;
    ctx.globalAlpha = 0.1;
    ctx.fill();
    ctx.globalAlpha = 1;

    // Step-function lines
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 1.5;
    for (let i = 0; i < sorted.length; i++) {
      const x = xFromTime(sorted[i].timeSeconds, cw, viewStart, viewEnd);
      const y = yFromBpm(sorted[i].bpm, ch, bpmMin, bpmMax);
      if (i === 0) {
        ctx.beginPath();
        ctx.moveTo(x, y);
      } else {
        const prevY = yFromBpm(sorted[i - 1].bpm, ch, bpmMin, bpmMax);
        ctx.lineTo(x, prevY);
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();

    // Points
    sorted.forEach((p) => {
      const x = xFromTime(p.timeSeconds, cw, viewStart, viewEnd);
      const y = yFromBpm(p.bpm, ch, bpmMin, bpmMax);
      const isSelected = selectedTime !== null && Math.abs(p.timeSeconds - selectedTime) < 0.001;
      const isHovered = hoverTime !== null && Math.abs(p.timeSeconds - hoverTime) < 0.001;

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
      ctx.fillStyle = isSelected ? "#fff" : theme.accent;
      ctx.fill();
    });
  }, [points, viewStart, viewEnd, bpmMin, bpmMax, selectedTime, hoverTime]);

  drawRef.current = draw;

  // Redraw when state changes
  useEffect(() => {
    draw();
  }, [draw]);

  const getPointAt = useCallback(
    (mx: number, my: number): TempoPointSnapshot | null => {
      const { w: cw, h: ch } = sizeRef.current;
      const sorted = [...points].sort((a, b) => a.timeSeconds - b.timeSeconds);
      for (const p of sorted) {
        const px = xFromTime(p.timeSeconds, cw, viewStart, viewEnd);
        const py = yFromBpm(p.bpm, ch, bpmMin, bpmMax);
        if (distToPoint(mx, my, px, py) < HIT_RADIUS) {
          return p;
        }
      }
      return null;
    },
    [points, viewStart, viewEnd, bpmMin, bpmMax]
  );

  const handleMouseDown = useCallback(
    (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (e.button !== 0) return;
      const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const { w: cw, h: ch } = sizeRef.current;

      const hitPoint = getPointAt(mx, my);
      if (hitPoint !== null) {
        setSelectedTime(hitPoint.timeSeconds);
        const unsortedIdx = points.indexOf(hitPoint);
        const px = xFromTime(hitPoint.timeSeconds, cw, viewStart, viewEnd);
        const py = yFromBpm(hitPoint.bpm, ch, bpmMin, bpmMax);
        setDragState({
          unsortedIndex: unsortedIdx,
          startTime: hitPoint.timeSeconds,
          startBpm: hitPoint.bpm,
          offsetX: mx - px,
          offsetY: my - py,
        });
      } else {
        // Add new tempo point
        const t = timeFromX(mx, cw, viewStart, viewEnd);
        const b = bpmFromY(my, ch, bpmMin, bpmMax);
        const clampedBpm = Math.max(bpmMin, Math.min(bpmMax, Math.round(b)));
        rpc
          .call("project.addTempoPoint", { timeSeconds: Math.max(0, t), bpm: clampedBpm })
          .then(() => refresh())
          .catch(console.error);
        setSelectedTime(null);
      }
    },
    [getPointAt, points, viewStart, viewEnd, bpmMin, bpmMax, rpc, refresh]
  );

  // Window-level drag handlers
  useEffect(() => {
    if (!dragState) return;

    const handleWindowMouseMove = (e: MouseEvent) => {
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const { w: cw, h: ch } = sizeRef.current;

      const newTime = Math.max(0, timeFromX(mx - dragState.offsetX, cw, viewStart, viewEnd));
      const newBpm = Math.max(bpmMin, Math.min(bpmMax, Math.round(bpmFromY(my - dragState.offsetY, ch, bpmMin, bpmMax))));

      // Optimistic local update using unsorted index
      setPoints((prev) => {
        if (dragState.unsortedIndex >= prev.length) return prev;
        const next = [...prev];
        next[dragState.unsortedIndex] = { timeSeconds: newTime, bpm: newBpm };
        return next;
      });
    };

    const handleWindowMouseUp = async () => {
      const ds = dragState;
      setDragState(null);

      // Commit — the point at ds.unsortedIndex was updated optimistically
      // Read current points from state since the closure may be stale
      const currentPoints = usePointsRef.current;
      const currentPoint = currentPoints[ds.unsortedIndex];
      if (!currentPoint) return;

      const timeChanged = Math.abs(currentPoint.timeSeconds - ds.startTime) > 0.001;
      const bpmChanged = Math.abs(currentPoint.bpm - ds.startBpm) > 0.5;

      try {
        if (timeChanged) {
          await rpc.call("project.setTempoPointTime", { index: ds.unsortedIndex, timeSeconds: currentPoint.timeSeconds });
        }
        if (bpmChanged) {
          await rpc.call("project.setTempoPointBpm", { index: ds.unsortedIndex, bpm: currentPoint.bpm });
        }
        if (timeChanged || bpmChanged) {
          refresh();
        }
      } catch (err) {
        console.error("tempo point drag commit failed:", err);
        refresh(); // Revert on error
      }
    };

    window.addEventListener("mousemove", handleWindowMouseMove);
    window.addEventListener("mouseup", handleWindowMouseUp);
    return () => {
      window.removeEventListener("mousemove", handleWindowMouseMove);
      window.removeEventListener("mouseup", handleWindowMouseUp);
    };
  }, [dragState, viewStart, viewEnd, bpmMin, bpmMax, rpc, refresh]);

  const handleMouseMove = useCallback(
    (e: React.MouseEvent<HTMLCanvasElement>) => {
      const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      if (dragState) return; // Window handler takes over during drag

      const hitPoint = getPointAt(mx, my);
      setHoverTime(hitPoint?.timeSeconds ?? null);
      const el = canvasRef.current;
      if (el) {
        el.classList.toggle("te-hover-point", hitPoint !== null);
        el.classList.toggle("te-dragging", false);
      }
    },
    [dragState, getPointAt]
  );

  const handleContextMenu = useCallback(
    (e: React.MouseEvent<HTMLCanvasElement>) => {
      e.preventDefault();
      const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      const hitPoint = getPointAt(mx, my);
      if (hitPoint !== null) {
        const unsortedIdx = points.indexOf(hitPoint);
        if (unsortedIdx >= 0) {
          rpc
            .call("project.removeTempoPoint", { index: unsortedIdx })
            .then(() => {
              refresh();
              setSelectedTime(null);
            })
            .catch(console.error);
        }
      }
    },
    [getPointAt, points, rpc, refresh]
  );

  // Mouse wheel zoom
  const handleWheel = useCallback(
    (e: React.WheelEvent<HTMLCanvasElement>) => {
      e.preventDefault();
      const factor = e.deltaY > 0 ? 1.1 : 0.9;

      if (e.shiftKey) {
        // Zoom time range
        const center = (viewStart + viewEnd) / 2;
        const halfRange = ((viewEnd - viewStart) / 2) * factor;
        setViewStart(Math.max(0, center - halfRange));
        setViewEnd(center + halfRange);
      } else {
        // Zoom BPM range
        const center = (bpmMin + bpmMax) / 2;
        const halfRange = ((bpmMax - bpmMin) / 2) * factor;
        setBpmMin(Math.max(1, center - halfRange));
        setBpmMax(Math.min(999, center + halfRange));
      }
    },
    [viewStart, viewEnd, bpmMin, bpmMax]
  );

  return (
    <div className="tempo-editor">
      <div className="te-header">
        <span className="te-header-label">Tempo</span>
        <div className="te-zoom-controls">
          <span>{Math.round(bpmMin)}–{Math.round(bpmMax)} BPM</span>
          <button
            className="te-zoom-btn"
            title="Zoom in BPM"
            onClick={() => {
              const center = (bpmMin + bpmMax) / 2;
              const halfRange = ((bpmMax - bpmMin) / 2) * 0.8;
              setBpmMin(Math.max(1, center - halfRange));
              setBpmMax(Math.min(999, center + halfRange));
            }}
          >
            +
          </button>
          <button
            className="te-zoom-btn"
            title="Zoom out BPM"
            onClick={() => {
              const center = (bpmMin + bpmMax) / 2;
              const halfRange = ((bpmMax - bpmMin) / 2) * 1.25;
              setBpmMin(Math.max(1, center - halfRange));
              setBpmMax(Math.min(999, center + halfRange));
            }}
          >
            −
          </button>
        </div>
      </div>
      <div className="te-canvas-wrap">
        <canvas
          ref={canvasRef}
          className={`te-canvas${dragState ? " te-dragging" : ""}`}
          onMouseDown={handleMouseDown}
          onMouseMove={handleMouseMove}
          onMouseLeave={() => setHoverTime(null)}
          onContextMenu={handleContextMenu}
          onWheel={handleWheel}
        />
      </div>
    </div>
  );
}
