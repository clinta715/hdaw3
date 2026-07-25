import { useRef, useCallback, useEffect, useState } from "react";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "./snapUtils";
import { theme } from "../theme";
import "./CCLane.css";

interface CcPoint { ccId: number; controllerNumber: number; beat: number; value: number; }

interface CCLaneProps {
  clipId: number;
  controllerNumber: number;
  width: number;
  pixelsPerBeat: number;
  scrollX: number;
}

const H = 60;
const HIT_RADIUS = 6;

function hitTestPoint(
  clientX: number, clientY: number, el: HTMLCanvasElement,
  points: CcPoint[], scrollX: number, pixelsPerBeat: number,
): CcPoint | null {
  const rect = el.getBoundingClientRect();
  const x = clientX - rect.left;
  const y = clientY - rect.top;
  for (const p of points) {
    const px = p.beat * pixelsPerBeat - scrollX;
    const py = H - (p.value / 127) * H;
    if (Math.abs(x - px) <= HIT_RADIUS && Math.abs(y - py) <= HIT_RADIUS) return p;
  }
  return null;
}

function beatValueAt(
  clientX: number, clientY: number, el: HTMLCanvasElement,
  scrollX: number, pixelsPerBeat: number,
): { beat: number; value: number } {
  const rect = el.getBoundingClientRect();
  const rawBeat = (clientX - rect.left + scrollX) / pixelsPerBeat;
  const { snapEnabled, snapDivision } = useUiStore.getState();
  const beat = snapEnabled ? snapToGrid(rawBeat, snapDivision) : rawBeat;
  const value = Math.max(0, Math.min(127, Math.round(127 * (1 - (clientY - rect.top) / rect.height))));
  return { beat, value };
}

export default function CCLane({ clipId, controllerNumber, width, pixelsPerBeat, scrollX }: CCLaneProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [points, setPoints] = useState<CcPoint[]>([]);
  const dragRef = useRef<{ ccId: number; beat: number; value: number } | null>(null);
  const movedRef = useRef(false);

  // Re-fetch CC points when clip or controller changes. The consumer
  // (PianoRoll) re-mounts the lane on tree change because it reads through
  // the project store snapshot, which refreshes after each committed edit.
  useEffect(() => {
    let cancelled = false;
    rpc.call("read.getCcPoints", { clipId, controllerNumber })
      .then((data) => {
        if (!cancelled && Array.isArray(data)) setPoints(data as CcPoint[]);
      })
      .catch(() => { if (!cancelled) setPoints([]); });
    return () => { cancelled = true; };
  }, [clipId, controllerNumber]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const h = H;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = h * dpr;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${h}px`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, h);

    ctx.fillStyle = theme.bgWidget;
    ctx.fillRect(0, 0, width, h);

    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(width, h / 2);
    ctx.stroke();

    if (points.length === 0) return;

    ctx.strokeStyle = theme.info;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    points.forEach((p, i) => {
      const x = p.beat * pixelsPerBeat - scrollX;
      const y = h - (p.value / 127) * h;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    ctx.fillStyle = theme.info;
    for (const p of points) {
      const x = p.beat * pixelsPerBeat - scrollX;
      const y = h - (p.value / 127) * h;
      ctx.beginPath();
      ctx.arc(x, y, 2.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }, [points, width, pixelsPerBeat, scrollX]);

  // Commit a drag on mouse-up anywhere (reads refs, so no stale closure).
  useEffect(() => {
    const onUp = () => {
      const drag = dragRef.current;
      if (drag && movedRef.current) {
        rpc.call("project.setCcPoint", { ccId: drag.ccId, beat: drag.beat, value: drag.value })
          .catch(console.error);
      }
      dragRef.current = null;
      movedRef.current = false;
    };
    window.addEventListener("mouseup", onUp);
    return () => window.removeEventListener("mouseup", onUp);
  }, []);

  const handleMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (e.button !== 0) return;
    const el = e.currentTarget;
    const hit = hitTestPoint(e.clientX, e.clientY, el, points, scrollX, pixelsPerBeat);
    if (hit) {
      dragRef.current = { ccId: hit.ccId, beat: hit.beat, value: hit.value };
      movedRef.current = false;
      return;
    }
    const { beat, value } = beatValueAt(e.clientX, e.clientY, el, scrollX, pixelsPerBeat);
    rpc.call("project.addCcPoint", { clipId, controllerNumber, beat, value }).catch(console.error);
    setPoints((prev) =>
      [...prev, { ccId: -1, controllerNumber, beat, value }].sort((a, b) => a.beat - b.beat)
    );
  }, [clipId, controllerNumber, pixelsPerBeat, scrollX, points]);

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag) return;
    const { beat, value } = beatValueAt(e.clientX, e.clientY, e.currentTarget, scrollX, pixelsPerBeat);
    movedRef.current = true;
    dragRef.current = { ccId: drag.ccId, beat, value };
    setPoints((prev) =>
      prev.map((p) => (p.ccId === drag.ccId ? { ...p, beat, value } : p)).sort((a, b) => a.beat - b.beat)
    );
  }, [pixelsPerBeat, scrollX]);

  const handleContextMenu = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const hit = hitTestPoint(e.clientX, e.clientY, e.currentTarget, points, scrollX, pixelsPerBeat);
    if (hit && hit.ccId >= 0) {
      rpc.call("project.removeCcPoint", { ccId: hit.ccId }).catch(console.error);
      setPoints((prev) => prev.filter((p) => p.ccId !== hit.ccId));
    }
  }, [points, pixelsPerBeat, scrollX]);

  return (
    <div className="cc-lane">
      <div className="cc-label">CC{controllerNumber}</div>
      <canvas
        ref={canvasRef}
        width={width}
        height={H}
        className="cc-canvas"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onContextMenu={handleContextMenu}
      />
    </div>
  );
}
