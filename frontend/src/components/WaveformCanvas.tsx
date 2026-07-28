import React from "react";
import { ClipSnapshot, WaveformPeaks } from "../rpc/types";
import { rpc } from "../rpc";
import { hexToRgba } from "../theme";

interface Props {
  clip: ClipSnapshot;
  width: number;
  height: number;
  /** Track color (#rrggbb) used to tint the waveform. Defaults to amber. */
  color?: string;
  onError?: (failed: boolean) => void;
}

// Cache key: clipId alone is too coarse. The peak data depends on the
// underlying source file AND any active timestretch (which changes audible
// duration vs. source duration). Including sourceFile + stretchRatio in the
// key means a re-render after timestretch or a file relink correctly
// re-fetches instead of returning the stale cached waveform.
function cacheKey(clip: ClipSnapshot): string {
  return `${clip.clipId}|${clip.sourceFile}|${clip.stretchMode}:${clip.stretchRatio.toFixed(4)}`;
}

// A peaks payload with no non-zero samples is almost never a real waveform —
// it's the signature of a transient backend read failure (the reader's cleared
// buffer comes back all-zeros when the source file is momentarily busy/locked,
// e.g. mid-timestretch render or an AV/indexer scan). Caching that would make a
// blank waveform stick until the cache key changes or the app reloads, so we
// treat it as "no data yet" and re-fetch instead of caching it.
function hasAudibleContent(peaks: WaveformPeaks): boolean {
  return peaks.peaks.length >= 2 && peaks.peaks.some((v) => v !== 0);
}

const peaksCache = new Map<string, WaveformPeaks>();

export const WaveformCanvas: React.FC<Props> = ({ clip, width, height, color = "#d99a4e", onError }) => {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const dpr = window.devicePixelRatio || 1;
  const key = cacheKey(clip);
  const [peaks, setPeaks] = React.useState<WaveformPeaks | null>(() => peaksCache.get(key) ?? null);
  const [fetched, setFetched] = React.useState(() => peaksCache.has(key));
  const [error, setError] = React.useState(false);

  React.useEffect(() => {
    if (peaksCache.has(key)) {
      setPeaks(peaksCache.get(key)!);
      setFetched(true);
      setError(false);
      return;
    }
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const attempt = (retriesLeft: number) => {
      rpc.call("read.getWaveformPeaks", { clipId: clip.clipId })
        .then((result) => {
          if (cancelled) return;
          const data = result as WaveformPeaks;
          if (hasAudibleContent(data)) {
            peaksCache.set(key, data);
            setPeaks(data);
            setFetched(true);
            setError(false);
          } else if (retriesLeft > 0) {
            // Silent/empty peaks — usually a transient backend read failure
            // (source file busy/locked). Don't cache it (that would stick the
            // blank waveform); show the flat line for now and retry shortly so
            // the waveform recovers once the file is readable again.
            setPeaks(data);
            timer = setTimeout(() => attempt(retriesLeft - 1), 500);
          } else {
            // Genuinely silent or still unreadable: show the flat line but leave
            // it uncached so the next open re-fetches fresh peak data.
            setPeaks(data);
            setFetched(true);
            setError(false);
          }
        })
        .catch(() => {
          if (!cancelled) {
            setFetched(true);
            setError(true);
            onError?.(true);
          }
        });
    };
    attempt(3);

    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
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
    grad.addColorStop(0, hexToRgba(color, 0.12));
    grad.addColorStop(0.5, hexToRgba(color, 0.32));
    grad.addColorStop(1, hexToRgba(color, 0.12));

    if (peaks && peaks.peaks.length >= 2) {
      const totalPairs = peaks.peaks.length / 2;

      // Slice peaks to the clip's audible range.  The peaks cover the entire
      // source file but the clip may start at an offset and/or be shorter.
      // For timestretched clips the offset is in source beats and the
      // duration in timestretched beats, so convert to source beats via
      // dividing by stretchRatio.
      const sourceDurationBeats = clip.sourceDuration * clip.sourceBpm / 60;
      const audibleSourceBeats = clip.stretchRatio > 0
        ? clip.durationBeats / clip.stretchRatio
        : clip.durationBeats;
      let startFrac = sourceDurationBeats > 0 ? clip.offset / sourceDurationBeats : 0;
      let endFrac = sourceDurationBeats > 0 ? (clip.offset + audibleSourceBeats) / sourceDurationBeats : 1;
      startFrac = Math.max(0, Math.min(1, startFrac));
      endFrac = Math.max(startFrac, Math.min(1, endFrac));

      const startPair = Math.floor(startFrac * totalPairs);
      const endPair = Math.max(startPair + 1, Math.ceil(endFrac * totalPairs));
      const slicePairs = endPair - startPair;

      const step = Math.max(1, Math.floor(slicePairs / width));
      const drawn: { x: number; min: number; max: number }[] = [];
      for (let i = startPair; i < endPair; i += step) {
        const idx = i * 2;
        const min = peaks.peaks[idx] ?? 0;
        const max = peaks.peaks[idx + 1] ?? 0;
        drawn.push({ x: ((i - startPair) / slicePairs) * width, min, max });
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

      ctx.strokeStyle = hexToRgba(color, 0.7);
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
      ctx.strokeStyle = hexToRgba(color, 0.3);
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, mid);
      ctx.lineTo(width, mid);
      ctx.stroke();
    }
  }, [clip, width, height, dpr, peaks, color]);

  if (error) {
    return (
      <div style={{ width, height, display: "flex", alignItems: "center", justifyContent: "center", background: "rgba(180, 40, 40, 0.15)", border: "1px solid rgba(220, 60, 60, 0.5)", borderRadius: 2 }}>
        <span style={{ fontSize: 9, color: "#e05555", fontWeight: 600, letterSpacing: 0.5 }}>FILE MISSING</span>
      </div>
    );
  }

  return <canvas ref={canvasRef} style={{ width, height, display: "block" }} />;
};
