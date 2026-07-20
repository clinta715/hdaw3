import { useRef, useCallback, useEffect, useState } from "react";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "./snapUtils";
import { theme } from "../theme";
import "./CCLane.css";

interface CcPoint { controllerNumber: number; beat: number; value: number; }

interface CCLaneProps {
  clipId: number;
  controllerNumber: number;
  width: number;
  pixelsPerBeat: number;
  scrollX: number;
}

export default function CCLane({ clipId, controllerNumber, width, pixelsPerBeat, scrollX }: CCLaneProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [points, setPoints] = useState<CcPoint[]>([]);

  // Re-fetch CC points when clip or controller changes. Re-fetch on every
  // treeChanged notification would be ideal; for now the consumer (PianoRoll)
  // re-mounts the lane on tree change because it reads through the project
  // store snapshot.
  useEffect(() => {
    let cancelled = false;
    rpc.call("read.getCcPoints", { clipId, controllerNumber })
      .then((data) => {
        if (!cancelled && Array.isArray(data)) setPoints(data as CcPoint[]);
      })
      .catch(() => { if (!cancelled) setPoints([]); });
    return () => { cancelled = true; };
  }, [clipId, controllerNumber]);

  // Redraw whenever points or view params change. Uses literal hex from
  // theme (canvas does not resolve CSS custom properties).
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const h = 60;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = h * dpr;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${h}px`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, h);

    // Background
    ctx.fillStyle = theme.bgWidget;
    ctx.fillRect(0, 0, width, h);

    // Mid line (value = 64)
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(width, h / 2);
    ctx.stroke();

    if (points.length === 0) return;

    // Connected line segments
    ctx.strokeStyle = theme.info;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    points.forEach((p, i) => {
      const x = p.beat * pixelsPerBeat - scrollX;
      // value 0 → bottom (h), value 127 → top (0)
      const y = h - (p.value / 127) * h;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Points
    ctx.fillStyle = theme.info;
    for (const p of points) {
      const x = p.beat * pixelsPerBeat - scrollX;
      const y = h - (p.value / 127) * h;
      ctx.beginPath();
      ctx.arc(x, y, 2.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }, [points, width, pixelsPerBeat, scrollX]);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left + scrollX;
    const rawBeat = x / pixelsPerBeat;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    const beat = snapEnabled ? snapToGrid(rawBeat, snapDivision) : rawBeat;
    const value = Math.round(127 * (1 - (e.clientY - rect.top) / rect.height));
    rpc.call("project.addCcPoint", {
      clipId,
      controllerNumber,
      beat,
      value: Math.max(0, Math.min(127, value)),
    }).catch(console.error);
    setPoints((prev) =>
      [...prev, { controllerNumber, beat, value: Math.max(0, Math.min(127, value)) }]
        .sort((a, b) => a.beat - b.beat)
    );
  }, [clipId, controllerNumber, pixelsPerBeat, scrollX]);

  return (
    <div className="cc-lane">
      <div className="cc-label">CC{controllerNumber}</div>
      <canvas
        ref={canvasRef}
        width={width}
        height={60}
        className="cc-canvas"
        onClick={handleCanvasClick}
      />
    </div>
  );
}
