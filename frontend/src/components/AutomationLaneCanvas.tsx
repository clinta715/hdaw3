import { useRef, useEffect, useState } from "react";
import { RpcClient } from "../rpc/client";
import { AutomationPointSnapshot } from "../rpc/types";
import "./AutomationLaneCanvas.css";

interface Props {
  laneName: string;
  points: AutomationPointSnapshot[];
  trackIndex: number;
  rpc: RpcClient;
  viewStartBeat: number;
  viewEndBeat: number;
  paramID: number;
  color?: string;
}

function buildPath(points: AutomationPointSnapshot[], vw: number, vh: number, viewStart: number, viewEnd: number): string {
  if (points.length === 0) return "";
  const range = viewEnd - viewStart;
  if (range <= 0) return "";
  const visible = points.filter((p) => p.time >= viewStart && p.time <= viewEnd);
  if (visible.length === 0) return "";
  const d = visible.map((p, i) => {
    const x = ((p.time - viewStart) / range) * vw;
    const y = (1 - p.value) * vh;
    return `${i === 0 ? "M" : "L"}${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
  return d;
}

export default function AutomationLaneCanvas({
  laneName,
  points,
  color = "var(--automation-line, #4fc3f7)",
}: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ w: 600, h: 80 });

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

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.scale(dpr, dpr);

    const w = size.w;
    const h = size.h;

    // Fill area under curve
    if (points.length > 1) {
      const path = buildPath(points, w, h, 0, 32);
      ctx.beginPath();
      ctx.moveTo(0, h);
      // dummy fill — simple polygon from first → each point → bottom-right
      const sorted = [...points].filter(p => p.time >= 0 && p.time <= 32).sort((a, b) => a.time - b.time);
      if (sorted.length >= 2) {
        const range = 32;
        ctx.moveTo(((sorted[0].time) / range) * w, h);
        sorted.forEach((p) => {
          const x = (p.time / range) * w;
          const y = (1 - p.value) * h;
          ctx.lineTo(x, y);
        });
        ctx.lineTo(((sorted[sorted.length - 1].time) / range) * w, h);
        ctx.closePath();
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.15;
        ctx.fill();
        ctx.globalAlpha = 1;
      }

      // Line
      ctx.beginPath();
      const linePath = buildPath(points, w, h, 0, 32);
      if (linePath) {
        const segments = new Path2D(linePath);
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.5;
        ctx.stroke(segments);
      }

      // Points as small circles
      sorted.forEach((p) => {
        const x = (p.time / 32) * w;
        const y = (1 - p.value) * h;
        ctx.beginPath();
        ctx.arc(x, y, 3, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();
      });
    }
  }, [points, size]);

  return (
    <div className="automation-lane-canvas">
      <div className="alc-header">{laneName}</div>
      <canvas ref={canvasRef} className="alc-canvas" />
    </div>
  );
}
