import { useState, useEffect, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./ModulationPanel.css";

interface Lfo {
  index: number;
  name: string;
  waveform: number;
  rate: number;
  rateSync: number;
  depth: number;
  bipolar: boolean;
  phaseOffset: number;
  targetParamID: number;
  enabled: boolean;
}

const WAVEFORMS = ["Sine", "Triangle", "Saw", "Square", "Random"];

export default function ModulationPanel() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [lfos, setLfos] = useState<Lfo[]>([]);

  const fetchLfos = useCallback(async () => {
    if (selectedTrackIndex == null) { setLfos([]); return; }
    try {
      const result = await rpc.call("read.getModulationLfos", { trackIndex: selectedTrackIndex }) as any[];
      setLfos(result);
    } catch { setLfos([]); }
  }, [selectedTrackIndex]);

  useEffect(() => { fetchLfos(); }, [fetchLfos]);

  const handleAddLfo = () => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.addLfo", { trackIndex: selectedTrackIndex }).then(fetchLfos);
  };

  const handleRemoveLfo = (lfoIndex: number) => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.removeLfo", { trackIndex: selectedTrackIndex, lfoIndex }).then(fetchLfos);
  };

  const handleSetParam = (lfoIndex: number, paramName: string, value: number | boolean) => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.setLfoParam", {
      trackIndex: selectedTrackIndex,
      lfoIndex,
      paramName,
      value: typeof value === "boolean" ? (value ? 1 : 0) : value,
    }).then(fetchLfos);
  };

  const trackName = snapshot?.tracks[selectedTrackIndex ?? -1]?.name ?? "No track selected";

  return (
    <div className="modulation-panel">
      <div className="mod-header">
        <span className="mod-title">Modulation — {trackName}</span>
        <button className="mod-add-btn" onClick={handleAddLfo}>+ Add LFO</button>
      </div>
      {lfos.length === 0 && (
        <div className="mod-empty">No LFOs. Click "+ Add LFO" to create one.</div>
      )}
      {lfos.map((lfo) => (
        <div key={lfo.index} className="mod-lfo-card">
          <div className="mod-lfo-header">
            <span className="mod-lfo-name">{lfo.name || `LFO ${lfo.index + 1}`}</span>
            <button className="mod-remove-btn" onClick={() => handleRemoveLfo(lfo.index)}>×</button>
          </div>
          <div className="mod-lfo-controls">
            <label>
              Wave
              <select
                value={lfo.waveform}
                onChange={(e) => handleSetParam(lfo.index, "waveform", Number(e.target.value))}
              >
                {WAVEFORMS.map((w, i) => (
                  <option key={i} value={i}>{w}</option>
                ))}
              </select>
            </label>
            <label>
              Rate
              <input
                type="range" min="0.1" max="20" step="0.1"
                value={lfo.rate}
                onChange={(e) => handleSetParam(lfo.index, "rate", Number(e.target.value))}
              />
              <span>{lfo.rate.toFixed(1)} Hz</span>
            </label>
            <label>
              Depth
              <input
                type="range" min="0" max="1" step="0.01"
                value={lfo.depth}
                onChange={(e) => handleSetParam(lfo.index, "depth", Number(e.target.value))}
              />
              <span>{Math.round(lfo.depth * 100)}%</span>
            </label>
            <label>
              Phase
              <input
                type="range" min="0" max="360" step="1"
                value={lfo.phaseOffset}
                onChange={(e) => handleSetParam(lfo.index, "phaseOffset", Number(e.target.value))}
              />
              <span>{Math.round(lfo.phaseOffset)}°</span>
            </label>
            <label className="mod-checkbox">
              <input
                type="checkbox"
                checked={lfo.bipolar}
                onChange={(e) => handleSetParam(lfo.index, "bipolar", e.target.checked)}
              />
              Bipolar
            </label>
            <label className="mod-checkbox">
              <input
                type="checkbox"
                checked={lfo.enabled}
                onChange={(e) => handleSetParam(lfo.index, "enabled", e.target.checked)}
              />
              Enabled
            </label>
          </div>
        </div>
      ))}
    </div>
  );
}
