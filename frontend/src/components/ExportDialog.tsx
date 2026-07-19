import { useState, useEffect, useRef } from "react";
import { rpc } from "../rpc";
import "./ExportDialog.css";

interface ExportDialogProps {
  onClose: () => void;
}

export default function ExportDialog({ onClose }: ExportDialogProps) {
  const [outputPath, setOutputPath] = useState("export.wav");
  const [format, setFormat] = useState("wav");
  const [bitDepth, setBitDepth] = useState(24);
  const [progress, setProgress] = useState(0);
  const [status, setStatus] = useState("Ready");
  const [exporting, setExporting] = useState(false);
  const unsubRef = useRef<(() => void) | null>(null);

  useEffect(() => {
    return () => {
      unsubRef.current?.();
    };
  }, []);

  const handleExport = async () => {
    if (!outputPath.trim()) return;

    setExporting(true);
    setStatus("Exporting... 0%");
    setProgress(0);

    unsubRef.current = rpc.onNotification("notify.exportProgress", (_method, params) => {
      const p = params as { progress?: number } | undefined;
      if (p && typeof p.progress === "number") {
        const pct = Math.round(p.progress * 100);
        setProgress(pct);
        setStatus(`Exporting... ${pct}%`);
      }
    });

    try {
      await rpc.call("export.audio", {
        outputPath: outputPath.trim(),
        format,
        bitDepth,
      });
      setProgress(100);
      setStatus("Complete!");
    } catch {
      setStatus("Export failed");
    } finally {
      unsubRef.current?.();
      unsubRef.current = null;
      setExporting(false);
    }
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

        <div className="ed-progress-container">
          <div className="ed-progress-bar">
            <div className="ed-progress-fill" style={{ width: `${progress}%` }} />
          </div>
          <div className="ed-status">{status}</div>
        </div>

        <div className="ed-buttons">
          <button className="ed-btn ed-btn-cancel" onClick={onClose}>
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
