import { useState, useEffect, useRef } from "react";
import { rpc } from "../rpc";
import { reportRpcError } from "../store/notifyStore";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useMarkerStore } from "../store/markerStore";
import "./ExportDialog.css";

interface ExportDialogProps {
  onClose: () => void;
}

type ExportRange = "full" | "loop" | "selection" | "markers";

const SAMPLE_RATES = [44100, 48000, 88200, 96000];
const PREFS_KEY = "hdaw.exportPrefs";

interface ExportPrefs {
  format: string;
  bitDepth: number;
  sampleRate: number;
  range: ExportRange;
}

function loadPrefs(): ExportPrefs {
  const fallback: ExportPrefs = { format: "wav", bitDepth: 24, sampleRate: 48000, range: "full" };
  try {
    const raw = localStorage.getItem(PREFS_KEY);
    if (!raw) return fallback;
    const p = JSON.parse(raw) as Partial<ExportPrefs>;
    return {
      format: p.format === "aiff" || p.format === "flac" ? p.format : "wav",
      bitDepth: p.bitDepth === 16 || p.bitDepth === 32 ? p.bitDepth : 24,
      sampleRate: SAMPLE_RATES.includes(p.sampleRate ?? 0) ? (p.sampleRate as number) : 48000,
      range: (p.range === "loop" || p.range === "selection" || p.range === "markers") ? p.range : "full",
    };
  } catch {
    return fallback;
  }
}

export default function ExportDialog({ onClose }: ExportDialogProps) {
  const [initialPrefs] = useState(loadPrefs);
  const [outputPath, setOutputPath] = useState("export.wav");
  const [format, setFormat] = useState(initialPrefs.format);
  const [bitDepth, setBitDepth] = useState(initialPrefs.bitDepth);
  const [sampleRate, setSampleRate] = useState(initialPrefs.sampleRate);
  const [range, setRange] = useState<ExportRange>(initialPrefs.range);
  const [progress, setProgress] = useState(0);
  const [status, setStatus] = useState("Ready");
  const [exporting, setExporting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const unsubRef = useRef<(() => void) | null>(null);
  const cancellingRef = useRef(false);

  const transport = useTransportStore((s) => s.transport);
  const loopEnabled = transport.isLooping && transport.loopEnd > transport.loopStart;

  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const markers = useMarkerStore((s) => s.markers);

  const hasSelection = selectedClipIds.size > 0 && snapshot?.clips.some((c) => selectedClipIds.has(c.clipId));
  const hasMarkers = markers.length >= 2;

  useEffect(() => {
    return () => {
      unsubRef.current?.();
    };
  }, []);

  useEffect(() => {
    try {
      localStorage.setItem(PREFS_KEY, JSON.stringify({ format, bitDepth, sampleRate, range }));
    } catch { /* ignore */ }
  }, [format, bitDepth, sampleRate, range]);

  const handleExport = async () => {
    if (!outputPath.trim()) return;

    setExporting(true);
    setCancelling(false);
    cancellingRef.current = false;
    setStatus("Exporting... 0%");
    setProgress(0);

    unsubRef.current = rpc.onNotification("notify.exportProgress", (_method, params) => {
      const p = params as { progress?: number } | undefined;
      if (p && typeof p.progress === "number") {
        const pct = Math.round(p.progress * 100);
        setProgress(pct);
        if (!cancellingRef.current) setStatus(`Exporting... ${pct}%`);
      }
    });

    const params: Record<string, unknown> = {
      outputPath: outputPath.trim(),
      format,
      bitDepth,
      sampleRate,
    };
    if (range === "loop") {
      const t = useTransportStore.getState().transport;
      if (t.isLooping && t.loopEnd > t.loopStart && t.bpm > 0) {
        params.start = (t.loopStart * 60) / t.bpm;
        params.end = (t.loopEnd * 60) / t.bpm;
      }
    }
    if (range === "selection" && snapshot) {
      const selectedClips = snapshot.clips.filter((c) => selectedClipIds.has(c.clipId));
      if (selectedClips.length > 0) {
        const bpm = transport.bpm || 120;
        const minBeat = Math.min(...selectedClips.map((c) => c.startBeat));
        const maxBeat = Math.max(...selectedClips.map((c) => c.startBeat + c.durationBeats));
        params.start = (minBeat * 60) / bpm;
        params.end = (maxBeat * 60) / bpm;
      }
    }
    if (range === "markers" && markers.length >= 2) {
      const bpm = transport.bpm || 120;
      const sorted = [...markers].sort((a, b) => a.time - b.time);
      params.start = (sorted[0].time * 60) / bpm;
      params.end = (sorted[sorted.length - 1].time * 60) / bpm;
    }

    try {
      await rpc.call("export.audio", params);
      if (cancellingRef.current) {
        setProgress(0);
        setStatus("Ready");
      } else {
        setProgress(100);
        setStatus("Complete!");
      }
    } catch (err) {
      if (cancellingRef.current) {
        setProgress(0);
        setStatus("Ready");
      } else {
        setStatus("Export failed");
        reportRpcError("export.audio", err);
      }
    } finally {
      unsubRef.current?.();
      unsubRef.current = null;
      setExporting(false);
      setCancelling(false);
      cancellingRef.current = false;
    }
  };

  const handleCancel = () => {
    if (!exporting) {
      onClose();
      return;
    }
    if (cancellingRef.current) return;
    cancellingRef.current = true;
    setCancelling(true);
    setStatus("Cancelling…");
    rpc.call("export.cancel", {}).catch(() => {});
  };

  const pathBase = outputPath.replace(/\.(wav|aiff|flac)$/i, "");

  return (
    <div className="ed-overlay" onClick={onClose}>
      <div className="ed-dialog" onClick={(e) => e.stopPropagation()}>
        <h3 className="ed-title">Export Audio</h3>

        <div className="ed-row">
          <span className="ed-label">Output</span>
          <input
            className="ed-input"
            type="text"
            value={outputPath}
            onChange={(e) => setOutputPath(e.target.value)}
            disabled={exporting}
          />
        </div>

        <div className="ed-row">
          <span className="ed-label">Format</span>
          <select
            className="ed-select"
            value={format}
            onChange={(e) => {
              setFormat(e.target.value);
              setOutputPath(pathBase + (e.target.value === "aiff" ? ".aiff" : e.target.value === "flac" ? ".flac" : ".wav"));
            }}
            disabled={exporting}
          >
            <option value="wav">WAV</option>
            <option value="aiff">AIFF</option>
            <option value="flac">FLAC</option>
          </select>
        </div>

        <div className="ed-row">
          <span className="ed-label">Bit Depth</span>
          <select
            className="ed-select"
            value={bitDepth}
            onChange={(e) => setBitDepth(Number(e.target.value))}
            disabled={exporting}
          >
            <option value={16}>16-bit</option>
            <option value={24}>24-bit</option>
            <option value={32}>32-bit float</option>
          </select>
        </div>

        <div className="ed-row">
          <span className="ed-label">Sample Rate</span>
          <select
            className="ed-select"
            value={sampleRate}
            onChange={(e) => setSampleRate(Number(e.target.value))}
            disabled={exporting}
          >
            {SAMPLE_RATES.map((r) => (
              <option key={r} value={r}>{r} Hz</option>
            ))}
          </select>
        </div>

        <div className="ed-row">
          <span className="ed-label">Range</span>
          <select
            className="ed-select"
            value={range}
            onChange={(e) => setRange(e.target.value as ExportRange)}
            disabled={exporting}
          >
            <option value="full">Full Project</option>
            <option value="loop" disabled={!loopEnabled}>Loop Region</option>
            <option value="selection" disabled={!hasSelection}>Selection</option>
            <option value="markers" disabled={!hasMarkers}>Between Markers</option>
          </select>
        </div>

        <div className="ed-progress-container">
          <div className="ed-progress-bar">
            <div className="ed-progress-fill" style={{ width: `${progress}%` }} />
          </div>
          <div className="ed-status">{status}</div>
        </div>

        <div className="ed-buttons">
          <button className="ed-btn ed-btn-cancel" onClick={handleCancel} disabled={cancelling}>
            {exporting ? "Cancel" : "Close"}
          </button>
          <button
            className="ed-btn ed-btn-export"
            onClick={handleExport}
            disabled={exporting || !outputPath.trim()}
          >
            Export
          </button>
        </div>
      </div>
    </div>
  );
}
