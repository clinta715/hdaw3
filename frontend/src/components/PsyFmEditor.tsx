import React, { useState, useEffect, useCallback } from "react";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./PsyFmEditor.css";

const PRESETS = ["growlBass", "acidLead", "metallicPluck", "riser"] as const;
const PRESET_LABELS: Record<string, string> = {
  growlBass: "Growl Bass",
  acidLead: "Acid Lead",
  metallicPluck: "Metallic Pluck",
  riser: "Riser",
};

interface AnalysisState {
  activeVoices: number;
  opEgLevels: number[];
}

interface FxSlot {
  fxType: string;
  params: { index: number; name: string; value: number; defaultValue: number; minValue: number; maxValue: number }[];
}

export default function PsyFmEditor() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [slotIndex, setSlotIndex] = useState<number>(-1);
  const [params, setParams] = useState<FxSlot["params"]>([]);
  const [analysis, setAnalysis] = useState<AnalysisState | null>(null);

  // Load FX slot state
  useEffect(() => {
    if (selectedTrackIndex == null) { setSlotIndex(-1); setParams([]); setAnalysis(null); return; }
    rpc.call("read.getFxSlots", { trackIndex: selectedTrackIndex })
      .then((data) => {
        if (!Array.isArray(data)) { setSlotIndex(-1); return; }
        const slots = data as FxSlot[];
        const idx = slots.findIndex((s) => s.fxType === "psy_fm");
        setSlotIndex(idx);
        if (idx >= 0) {
          setParams(slots[idx].params || []);
          // Load analysis
          rpc.call("read.getInternalFxParams", { trackIndex: selectedTrackIndex, slotIndex: idx })
            .then((d: any) => { if (d?.params) setParams(d.params); })
            .catch(() => {});
        }
      })
      .catch(() => { setSlotIndex(-1); setParams([]); });
  }, [selectedTrackIndex]);

  // Poll analysis data
  useEffect(() => {
    if (selectedTrackIndex == null || slotIndex < 0) { setAnalysis(null); return; }
    const interval = setInterval(() => {
      rpc.call("psy_fm.getAnalysis", { trackIndex: selectedTrackIndex, slotIndex })
        .then((d: any) => setAnalysis(d as AnalysisState))
        .catch(() => {});
    }, 100);
    return () => clearInterval(interval);
  }, [selectedTrackIndex, slotIndex]);

  const setParam = useCallback(async (paramIndex: number, value: number) => {
    if (selectedTrackIndex == null || slotIndex < 0) return;
    await rpc.call("set_internal_fx_param", {
      trackIndex: selectedTrackIndex, slotIndex, paramIndex, value,
    });
    // Refresh params
    rpc.call("read.getInternalFxParams", { trackIndex: selectedTrackIndex, slotIndex })
      .then((d: any) => { if (d?.params) setParams(d.params); })
      .catch(() => {});
  }, [selectedTrackIndex, slotIndex]);

  const loadPreset = useCallback(async (preset: string) => {
    if (selectedTrackIndex == null || slotIndex < 0) return;
    await rpc.call("psy_fm.loadPreset", {
      trackIndex: selectedTrackIndex, slotIndex, preset,
    });
    // Refresh params
    rpc.call("read.getInternalFxParams", { trackIndex: selectedTrackIndex, slotIndex })
      .then((d: any) => { if (d?.params) setParams(d.params); })
      .catch(() => {});
  }, [selectedTrackIndex, slotIndex]);

  if (selectedTrackIndex == null || slotIndex < 0) {
    return <div className="psyfm-editor psyfm-editor--empty">Select a track with a Psy FM synth</div>;
  }

  const getParam = (index: number) => params.find((p) => p.index === index);
  const getVal = (index: number, def = 0) => getParam(index)?.value ?? def;

  const algorithmPreset = Math.round(getVal(32, 0));

  return (
    <div className="psyfm-editor">
      {/* Preset selector */}
      <div className="psyfm-presets">
        <span className="psyfm-label">Preset</span>
        <div className="psyfm-preset-buttons">
          {PRESETS.map((p) => (
            <button
              key={p}
              className={`psyfm-preset-btn ${algorithmPreset === PRESETS.indexOf(p) ? "psyfm-preset-btn--active" : ""}`}
              onClick={() => loadPreset(p)}
              title={PRESET_LABELS[p]}
            >
              {PRESET_LABELS[p]}
            </button>
          ))}
        </div>
      </div>

      <div className="psyfm-body">
        {/* Operator ratios */}
        <div className="psyfm-section">
          <div className="psyfm-section-title">Ratios</div>
          <div className="psyfm-knobs">
            {[0, 1, 2, 3, 4, 5].map((i) => (
              <div key={i} className="psyfm-knob-group">
                <label className="psyfm-knob-label">OP{i + 1}</label>
                <input
                  type="range"
                  className="psyfm-slider psyfm-slider--ratio"
                  min={0.1} max={10} step={0.1}
                  value={getVal(i, 1)}
                  onChange={(e) => setParam(i, parseFloat(e.target.value))}
                />
                <span className="psyfm-knob-value">{getVal(i, 1).toFixed(1)}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Feedback + Output */}
        <div className="psyfm-section">
          <div className="psyfm-section-title">Global</div>
          <div className="psyfm-knobs">
            <div className="psyfm-knob-group">
              <label className="psyfm-knob-label">Fb</label>
              <input
                type="range"
                className="psyfm-slider"
                min={0} max={1} step={0.01}
                value={getVal(6, 0)}
                onChange={(e) => setParam(6, parseFloat(e.target.value))}
              />
              <span className="psyfm-knob-value">{getVal(6, 0).toFixed(2)}</span>
            </div>
            <div className="psyfm-knob-group">
              <label className="psyfm-knob-label">Level</label>
              <input
                type="range"
                className="psyfm-slider"
                min={0} max={1} step={0.01}
                value={getVal(31, 0.4)}
                onChange={(e) => setParam(31, parseFloat(e.target.value))}
              />
              <span className="psyfm-knob-value">{getVal(31, 0.4).toFixed(2)}</span>
            </div>
          </div>
        </div>

        {/* Operator envelopes (compact) */}
        <div className="psyfm-section">
          <div className="psyfm-section-title">Envelopes</div>
          <div className="psyfm-env-grid">
            {[0, 1, 2, 3, 4, 5].map((op) => {
              const base = 7 + op * 4;
              return (
                <div key={op} className="psyfm-env-col">
                  <div className="psyfm-env-header">OP{op + 1}</div>
                  <div className="psyfm-env-row">
                    <label>A</label>
                    <input type="range" className="psyfm-slider--small"
                      min={0.001} max={2} step={0.001}
                      value={getVal(base, 0.01)}
                      onChange={(e) => setParam(base, parseFloat(e.target.value))} />
                  </div>
                  <div className="psyfm-env-row">
                    <label>D</label>
                    <input type="range" className="psyfm-slider--small"
                      min={0.001} max={5} step={0.001}
                      value={getVal(base + 1, 0.3)}
                      onChange={(e) => setParam(base + 1, parseFloat(e.target.value))} />
                  </div>
                  <div className="psyfm-env-row">
                    <label>S</label>
                    <input type="range" className="psyfm-slider--small"
                      min={0} max={1} step={0.01}
                      value={getVal(base + 2, 0.7)}
                      onChange={(e) => setParam(base + 2, parseFloat(e.target.value))} />
                  </div>
                  <div className="psyfm-env-row">
                    <label>R</label>
                    <input type="range" className="psyfm-slider--small"
                      min={0.001} max={5} step={0.001}
                      value={getVal(base + 3, 0.2)}
                      onChange={(e) => setParam(base + 3, parseFloat(e.target.value))} />
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Analysis */}
        {analysis && (
          <div className="psyfm-section">
            <div className="psyfm-section-title">Analysis</div>
            <div className="psyfm-analysis">
              <span className="psyfm-analysis-item">Voices: {analysis.activeVoices}</span>
              <div className="psyfm-op-levels">
                {(analysis.opEgLevels || []).map((level, i) => (
                  <div key={i} className="psyfm-level-bar">
                    <div className="psyfm-level-label">OP{i + 1}</div>
                    <div className="psyfm-level-track">
                      <div className="psyfm-level-fill" style={{ width: `${Math.min(100, level * 100)}%` }} />
                    </div>
                  </div>
                ))}
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
