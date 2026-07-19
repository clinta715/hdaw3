import { useState } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import "./ImportDialog.css";

interface ImportDialogProps {
  mode: "audio" | "midi";
  onClose: () => void;
  onImport: () => void;
}

export default function ImportDialog({ mode, onClose, onImport }: ImportDialogProps) {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];

  const [filePath, setFilePath] = useState("");
  const [trackChoice, setTrackChoice] = useState("new");

  const handleBrowse = () => {
    const path = window.prompt(
      mode === "audio" ? "Enter audio file path:" : "Enter MIDI file path:"
    );
    if (path) setFilePath(path);
  };

  const handleImport = async () => {
    if (!filePath.trim()) return;

    const method = mode === "audio" ? "project.addAudioClip" : "project.addMidiClip";
    const params: Record<string, unknown> = { sourceFile: filePath.trim() };

    if (trackChoice !== "new") {
      params.trackIndex = parseInt(trackChoice, 10);
    }

    await rpc.call(method, params).catch(() => {});
    onImport();
    onClose();
  };

  return (
    <div className="id-overlay" onClick={onClose}>
      <div className="id-dialog" onClick={(e) => e.stopPropagation()}>
        <h3 className="id-title">
          {mode === "audio" ? "Import Audio" : "Import MIDI"}
        </h3>

        <div className="id-row">
          <span className="id-label">File</span>
          <input
            className="id-input"
            type="text"
            value={filePath}
            onChange={(e) => setFilePath(e.target.value)}
            placeholder={mode === "audio" ? "path/to/audio.wav" : "path/to/file.mid"}
          />
          <button className="id-browse" onClick={handleBrowse}>Browse</button>
        </div>

        <div className="id-row">
          <span className="id-label">Track</span>
          <select
            className="id-select"
            value={trackChoice}
            onChange={(e) => setTrackChoice(e.target.value)}
          >
            <option value="new">New Track</option>
            {tracks.map((t) => (
              <option key={t.index} value={t.index}>
                {t.index}: {t.name}
              </option>
            ))}
          </select>
        </div>

        <div className="id-buttons">
          <button className="id-btn id-btn-cancel" onClick={onClose}>Cancel</button>
          <button className="id-btn id-btn-import" onClick={handleImport}>Import</button>
        </div>
      </div>
    </div>
  );
}
