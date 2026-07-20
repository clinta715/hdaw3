import { useEffect, useRef, useCallback } from "react";
import { GainEnvelopePoint } from "../rpc/types";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "./snapUtils";
import { theme } from "../theme";
import "./GainEnvelopeEditor.css";

interface Props {
  clipId: number;
  points: GainEnvelopePoint[];
  durationBeats: number;
}

const CANVAS_H = 80;
const PAD = 8;

// Shared draw routine used by both the live drag preview and the data-driven
// repaint. NOTE: Canvas 2D contexts do NOT resolve CSS custom properties —
// `ctx.fillStyle = "var(--accent)"` silently no-ops and leaves the prior
// fillStyle in place. Always pass resolved hex strings (here from `theme`).
function drawEnvelope(
  ctx: CanvasRenderingContext2D,
  w: number,
  h: number,
  pts: GainEnvelopePoint[],
  durationBeats: number,
) {
  ctx.clearRect(0, 0, w, h);

  const plotW = w - PAD * 2;
  const plotH = h - PAD * 2;

  // Background
  ctx.fillStyle = theme.bgWidget;
  ctx.fillRect(0, 0, w, h);

  // Grid lines (horizontal at gain=0, 0.5, 1.0, 1.5, 2.0)
  for (let g = 0; g <= 2; g += 0.5) {
    const y = h - PAD - (g / 2) * plotH;
    ctx.strokeStyle = g === 1 ? "rgba(255,255,255,0.12)" : "rgba(255,255,255,0.05)";
    ctx.lineWidth = g === 1 ? 1 : 0.5;
    ctx.beginPath();
    ctx.moveTo(PAD, y);
    ctx.lineTo(w - PAD, y);
    ctx.stroke();
  }

  // Grid lines (vertical per beat)
  for (let b = 0; b <= Math.ceil(durationBeats); b++) {
    const x = PAD + (b / durationBeats) * plotW;
    ctx.strokeStyle = "rgba(255,255,255,0.04)";
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    ctx.moveTo(x, PAD);
    ctx.lineTo(x, h - PAD);
    ctx.stroke();
  }

  // Points — must have at least the implicit start/end points
  const drawPts = pts.length > 0 ? pts : [{ time: 0, gain: 1 }, { time: durationBeats, gain: 1 }];

  // Curve
  if (drawPts.length >= 2) {
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < drawPts.length; i++) {
      const x = PAD + (drawPts[i].time / durationBeats) * plotW;
      const y = h - PAD - (drawPts[i].gain / 2) * plotH;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  // Points
  for (const p of drawPts) {
    const x = PAD + (p.time / durationBeats) * plotW;
    const y = h - PAD - (p.gain / 2) * plotH;
    ctx.fillStyle = "#fff";
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 2;
    ctx.stroke();
  }
}

export default function GainEnvelopeEditor({ clipId, points, durationBeats }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ idx: number } | null>(null);
  const ptsRef = useRef<GainEnvelopePoint[]>(points);
  ptsRef.current = points;

  const toCanvas = useCallback(
    (clientX: number, clientY: number) => {
      const canvas = canvasRef.current;
      if (!canvas) return { time: 0, gain: 0 };
      const rect = canvas.getBoundingClientRect();
      const w = rect.width;
      const h = rect.height;
      const plotW = w - PAD * 2;
      const plotH = h - PAD * 2;
      const x = clientX - rect.left;
      const y = clientY - rect.top;
      const rawTime = Math.max(0, Math.min(durationBeats, ((x - PAD) / plotW) * durationBeats));
      const { snapEnabled, snapDivision } = useUiStore.getState();
      const time = snapEnabled ? snapToGrid(rawTime, snapDivision) : rawTime;
      const gain = Math.max(0, Math.min(2, ((h - PAD - y) / plotH) * 2));
      return { time, gain };
    },
    [durationBeats]
  );

  const findNearest = useCallback(
    (clientX: number, clientY: number) => {
      const canvas = canvasRef.current;
      if (!canvas) return -1;
      const rect = canvas.getBoundingClientRect();
      const w = rect.width;
      const h = rect.height;
      const plotW = w - PAD * 2;
      const plotH = h - PAD * 2;
      const mx = clientX - rect.left;
      const my = clientY - rect.top;
      for (let i = 0; i < ptsRef.current.length; i++) {
        const px = PAD + (ptsRef.current[i].time / durationBeats) * plotW;
        const py = h - PAD - (ptsRef.current[i].gain / 2) * plotH;
        const dx = mx - px;
        const dy = my - py;
        if (Math.sqrt(dx * dx + dy * dy) <= 10) return i;
      }
      return -1;
    },
    [durationBeats]
  );

  const handleMouseDown = useCallback(
    (e: React.MouseEvent) => {
      const idx = findNearest(e.clientX, e.clientY);
      if (idx >= 0) {
        dragRef.current = { idx };
      } else {
        const { time, gain } = toCanvas(e.clientX, e.clientY);
        const newPts = [...ptsRef.current, { time, gain }].sort((a, b) => a.time - b.time);
        rpc.call("project.setClipGainEnvelope", { clipId, points: newPts }).catch(console.error);
      }
    },
    [clipId, findNearest, toCanvas]
  );

  const handleWindowMouseMove = useCallback(
    (e: MouseEvent) => {
      const drag = dragRef.current;
      if (!drag) return;
      const canvas = canvasRef.current;
      if (!canvas) return;
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      const rect = canvas.getBoundingClientRect();
      const w = rect.width;
      const h = rect.height;
      const plotW = w - PAD * 2;
      const plotH = h - PAD * 2;
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      const rawTime = Math.max(0, Math.min(durationBeats, ((x - PAD) / plotW) * durationBeats));
      const { snapEnabled, snapDivision } = useUiStore.getState();
      const time = snapEnabled ? snapToGrid(rawTime, snapDivision) : rawTime;
      const gain = Math.max(0, Math.min(2, ((h - PAD - y) / plotH) * 2));
      const newPts = ptsRef.current.map((p, i) => (i === drag.idx ? { time, gain } : p));
      ptsRef.current = newPts;
      const dpr = window.devicePixelRatio || 1;
      canvas.width = w * dpr;
      canvas.height = h * dpr;
      ctx.scale(dpr, dpr);
      drawEnvelope(ctx, w, h, newPts, durationBeats);
    },
    [durationBeats]
  );

  const handleWindowMouseUp = useCallback(() => {
    const drag = dragRef.current;
    if (!drag) return;
    dragRef.current = null;
    // Points are already snapped in the preview via toCanvas, so ptsRef.current has snapped values
    rpc.call("project.setClipGainEnvelope", { clipId, points: ptsRef.current }).catch(console.error);
  }, [clipId]);

  useEffect(() => {
    window.addEventListener("mousemove", handleWindowMouseMove);
    window.addEventListener("mouseup", handleWindowMouseUp);
    return () => {
      window.removeEventListener("mousemove", handleWindowMouseMove);
      window.removeEventListener("mouseup", handleWindowMouseUp);
    };
  }, [handleWindowMouseMove, handleWindowMouseUp]);

  // Draw
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.scale(dpr, dpr);

    drawEnvelope(ctx, w, h, points, durationBeats);
  }, [points, durationBeats]);

  return (
    <canvas
      ref={canvasRef}
      className="gee-canvas"
      style={{ width: "100%", height: CANVAS_H, cursor: "crosshair" }}
      onMouseDown={handleMouseDown}
    />
  );
}
