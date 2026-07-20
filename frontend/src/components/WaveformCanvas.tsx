import React from "react";
import { ClipSnapshot, WaveformPeaks } from "../rpc/types";
import { rpc } from "../rpc";

interface Props {
  clip: ClipSnapshot;
  width: number;
  height: number;
}

// Cache key: clipId alone is too coarse. The peak data depends on the
// underlying source file AND any active timestretch (which changes audible
// duration vs. source duration). Including sourceFile + stretchRatio in the
// key means a re-render after timestretch or a file relink correctly
// re-fetches instead of returning the stale cached waveform.
function cacheKey(clip: ClipSnapshot): string {
  return `${clip.clipId}|${clip.sourceFile}|${clip.stretchMode}:${clip.stretchRatio.toFixed(4)}`;
}

const peaksCache = new Map<string, WaveformPeaks>();

export const WaveformCanvas: React.FC<Props> = ({ clip, width, height }) => {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const dpr = window.devicePixelRatio || 1;
  const key = cacheKey(clip);
  const [peaks, setPeaks] = React.useState<WaveformPeaks | null>(() => peaksCache.get(key) ?? null);
  const [fetched, setFetched] = React.useState(() => peaksCache.has(key));

  React.useEffect(() => {
    if (peaksCache.has(key)) {
      setPeaks(peaksCache.get(key)!);
      setFetched(true);
      return;
    }
    let cancelled = false;
    rpc.call("read.getWaveformPeaks", { clipId: clip.clipId })
      .then((result) => {
        if (cancelled) return;
        const data = result as WaveformPeaks;
        peaksCache.set(key, data);
        setPeaks(data);
        setFetched(true);
      })
      .catch(() => {
        if (!cancelled) setFetched(true);
      });
    return () => { cancelled = true; };
  }, [key, clip.clipId]);

  // Opportunistic GC: drop cache entries we haven't seen in a while. The Map
  // would otherwise grow forever in a long session as the user loads many
  // clips. Cap at 256 entries; on overflow, drop the oldest. (JS Map keeps
  // insertion order, so the first iterator value is the oldest.)
  React.useEffect(() => {
    if (peaksCache.size > 256) {
      const firstKey = peaksCache.keys().next().value;
      if (firstKey) peaksCache.delete(firstKey);
    }
  }, [key]);
  // fetched is consulted via state to allow a future "loading…" placeholder;
  // the canvas falls through to the flat-line fallback while null.
  void fetched;

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width <= 0 || height <= 0) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);

    const mid = height / 2;
    const grad = ctx.createLinearGradient(0, 0, 0, height);
    grad.addColorStop(0, "rgba(200, 140, 60, 0.15)");
    grad.addColorStop(0.5, "rgba(220, 160, 80, 0.35)");
    grad.addColorStop(1, "rgba(200, 140, 60, 0.15)");

    if (peaks && peaks.peaks.length >= 2) {
      const pairs = peaks.peaks.length / 2;
      const step = Math.max(1, Math.floor(pairs / width));
      const drawn: { x: number; min: number; max: number }[] = [];
      for (let i = 0; i < pairs; i += step) {
        const idx = i * 2;
        const min = peaks.peaks[idx];
        const max = peaks.peaks[idx + 1];
        drawn.push({ x: (i / pairs) * width, min, max });
      }
      if (drawn.length > 0 && drawn[drawn.length - 1].x < width - 1) {
        const last = drawn[drawn.length - 1];
        drawn.push({ x: width, min: last.min, max: last.max });
      }

      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.moveTo(0, mid);
      for (const p of drawn) {
        ctx.lineTo(p.x, mid - p.max * mid * 0.9);
      }
      for (let i = drawn.length - 1; i >= 0; i--) {
        ctx.lineTo(drawn[i].x, mid - drawn[i].min * mid * 0.9);
      }
      ctx.closePath();
      ctx.fill();

      ctx.strokeStyle = "rgba(230, 180, 100, 0.70)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let i = 0; i < drawn.length; i++) {
        const p = drawn[i];
        const y = mid - p.max * mid * 0.9;
        i === 0 ? ctx.moveTo(p.x, y) : ctx.lineTo(p.x, y);
      }
      ctx.stroke();
      ctx.beginPath();
      for (let i = 0; i < drawn.length; i++) {
        const p = drawn[i];
        const y = mid - p.min * mid * 0.9;
        i === 0 ? ctx.moveTo(p.x, y) : ctx.lineTo(p.x, y);
      }
      ctx.stroke();
    } else {
      // Fallback: subtle flat line when no peak data
      ctx.fillStyle = grad;
      ctx.fillRect(0, 0, width, height);
      ctx.strokeStyle = "rgba(230, 180, 100, 0.30)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, mid);
      ctx.lineTo(width, mid);
      ctx.stroke();
    }
  }, [clip, width, height, dpr, peaks]);

  return <canvas ref={canvasRef} style={{ width, height, display: "block" }} />;
};
