import React from "react";
import { ClipSnapshot } from "../rpc/types";

interface Props {
  clip: ClipSnapshot;
  width: number;
  height: number;
}

export const WaveformCanvas: React.FC<Props> = ({ clip, width, height }) => {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const dpr = window.devicePixelRatio || 1;

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width <= 0 || height <= 0) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);

    const grad = ctx.createLinearGradient(0, 0, 0, height);
    grad.addColorStop(0, "rgba(200, 140, 60, 0.25)");
    grad.addColorStop(0.5, "rgba(220, 160, 80, 0.50)");
    grad.addColorStop(1, "rgba(200, 140, 60, 0.25)");
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, width, height);

    const mid = height / 2;
    ctx.strokeStyle = "rgba(230, 180, 100, 0.70)";
    ctx.lineWidth = 1.5;
    const segments = Math.min(width, 120);

    ctx.beginPath();
    for (let i = 0; i < segments; i++) {
      const x = (i / segments) * width;
      const n = 0.5 + Math.sin(i * 0.3) * 0.25 + Math.sin(i * 0.7) * 0.15 + (Math.random() - 0.5) * 0.1;
      const y = mid + (n - 0.5) * height * 0.65;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();

    ctx.beginPath();
    for (let i = 0; i < segments; i++) {
      const x = (i / segments) * width;
      const n = 0.5 + Math.sin(i * 0.3) * 0.25 + Math.sin(i * 0.7) * 0.15 + (Math.random() - 0.5) * 0.1;
      const y = mid - (n - 0.5) * height * 0.65;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
  }, [clip, width, height, dpr]);

  return <canvas ref={canvasRef} style={{ width, height, display: "block" }} />;
};
