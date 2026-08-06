import { useState, useEffect, useCallback } from "react";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { reportRpcError } from "../store/notifyStore";
import { MidiFxSlotSnapshot } from "../rpc/types";
import "./MidiFxChain.css";

const MIDI_FX_TYPES = [
  { type: "arpeggiator", label: "Arpeggiator" },
  { type: "velocity", label: "Velocity" },
  { type: "chord", label: "Chord" },
  { type: "scale", label: "Scale Quantize" },
  { type: "notelength", label: "Note Length" },
  { type: "transpose", label: "Transpose" },
  { type: "keyfilter", label: "Key Filter" },
  { type: "multinote", label: "Multi-Note" },
  { type: "velocitycurve", label: "Velocity Curve" },
  { type: "notechance", label: "Note Chance" },
  { type: "mididelay", label: "MIDI Delay" },
  { type: "humanize", label: "Humanize" },
  { type: "strum", label: "Strum" },
];

const PARAM_CONFIGS: Record<string, { key: string; label: string; min: number; max: number; step: number; isText?: boolean }[]> = {
  arpeggiator: [
    { key: "arpRate", label: "Rate", min: 0.0625, max: 2, step: 0.0625 },
    { key: "arpPattern", label: "Pattern", min: 0, max: 2, step: 1 },
    { key: "arpOctaves", label: "Octaves", min: 1, max: 4, step: 1 },
    { key: "arpGate", label: "Gate", min: 0.1, max: 1, step: 0.05 },
  ],
  velocity: [
    { key: "velFactor", label: "Factor", min: 0, max: 2, step: 0.05 },
  ],
  chord: [
    { key: "chordType", label: "Type", min: 0, max: 3, step: 1 },
  ],
  scale: [
    { key: "scaleRoot", label: "Root", min: 0, max: 11, step: 1 },
    { key: "scaleType", label: "Scale", min: 0, max: 2, step: 1 },
  ],
  notelength: [
    { key: "lengthFactor", label: "Factor", min: 0.1, max: 4, step: 0.1 },
  ],
  transpose: [
    { key: "semitones", label: "Semitones", min: -24, max: 24, step: 1 },
  ],
  keyfilter: [
    { key: "keyFilterRoot", label: "Root", min: 0, max: 11, step: 1 },
    { key: "keyFilterScale", label: "Scale", min: 0, max: 2, step: 1 },
  ],
  multinote: [
    { key: "multiNoteIntervals", label: "Intervals", min: 0, max: 1, step: 1, isText: true },
  ],
  velocitycurve: [
    { key: "curveType", label: "Curve", min: 0, max: 4, step: 1 },
    { key: "curveAmount", label: "Amount", min: 0, max: 1, step: 0.05 },
  ],
  notechance: [
    { key: "noteChance", label: "Probability", min: 0, max: 1, step: 0.05 },
  ],
  mididelay: [
    { key: "delayBeats", label: "Delay", min: 0.0625, max: 2, step: 0.0625 },
    { key: "delayFeedback", label: "Feedback", min: 0, max: 0.95, step: 0.05 },
    { key: "delayMix", label: "Mix", min: 0, max: 1, step: 0.05 },
  ],
  humanize: [
    { key: "humanizeTiming", label: "Timing", min: 0, max: 1, step: 0.05 },
    { key: "humanizeVelocity", label: "Velocity", min: 0, max: 1, step: 0.05 },
    { key: "humanizePitch", label: "Pitch", min: 0, max: 1, step: 0.05 },
  ],
  strum: [
    { key: "strumTime", label: "Time", min: 0, max: 0.5, step: 0.005 },
    { key: "strumDirection", label: "Dir", min: 0, max: 2, step: 1 },
  ],
};

function labelFor(fxType: string): string {
  return MIDI_FX_TYPES.find((t) => t.type === fxType)?.label ?? fxType;
}

export default function MidiFxChain() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [slots, setSlots] = useState<MidiFxSlotSnapshot[]>([]);
  const [refreshKey, setRefreshKey] = useState(0);
  const refresh = useCallback(() => setRefreshKey((k) => k + 1), []);

  useEffect(() => {
    if (selectedTrackIndex == null) {
      setSlots([]);
      return;
    }
    let cancelled = false;
    rpc.call("read.getMidiFxSlots", { trackIndex: selectedTrackIndex })
      .then((data) => {
        if (!cancelled) setSlots(Array.isArray(data) ? (data as MidiFxSlotSnapshot[]) : []);
      })
      .catch(() => { if (!cancelled) setSlots([]); });
    return () => { cancelled = true; };
  }, [selectedTrackIndex, refreshKey]);

  const addSlot = useCallback(async (fxType: string) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.addMidiFxSlot", { trackIndex: selectedTrackIndex, type: fxType });
      refresh();
    } catch (err) {
      reportRpcError("project.addMidiFxSlot", err);
    }
  }, [selectedTrackIndex, refresh]);

  const removeSlot = useCallback(async (slotIndex: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.removeMidiFxSlot", { trackIndex: selectedTrackIndex, slotIndex });
      refresh();
    } catch (err) {
      reportRpcError("project.removeMidiFxSlot", err);
    }
  }, [selectedTrackIndex, refresh]);

  const toggleBypass = useCallback(async (slot: MidiFxSlotSnapshot) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.setMidiFxSlotBypassed", {
        trackIndex: selectedTrackIndex,
        slotIndex: slot.slotIndex,
        bypassed: !slot.bypassed,
      });
      refresh();
    } catch (err) {
      reportRpcError("project.setMidiFxSlotBypassed", err);
    }
  }, [selectedTrackIndex, refresh]);

  const setParam = useCallback(async (slotIndex: number, paramName: string, value: number | string) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.setMidiFxSlotParam", {
        trackIndex: selectedTrackIndex,
        slotIndex,
        paramName,
        value,
      });
      refresh();
    } catch (err) {
      reportRpcError("project.setMidiFxSlotParam", err);
    }
  }, [selectedTrackIndex, refresh]);

  if (selectedTrackIndex == null) {
    return (
      <div className="midi-fx-chain">
        <span className="mfx-empty">Select a track to edit MIDI FX</span>
      </div>
    );
  }

  return (
    <div className="midi-fx-chain">
      <div className="mfx-header">
        <span className="mfx-title">MIDI FX — Track {selectedTrackIndex}</span>
        <select
          className="mfx-add-select"
          value=""
          onChange={(e) => { if (e.target.value) addSlot(e.target.value); }}
        >
          <option value="">+ Add MIDI FX…</option>
          {MIDI_FX_TYPES.map((t) => (
            <option key={t.type} value={t.type}>{t.label}</option>
          ))}
        </select>
      </div>
      <div className="mfx-slots">
        {slots.length === 0 && <span className="mfx-empty">No MIDI FX on this track</span>}
        {slots.map((slot) => (
          <div
            key={slot.slotIndex}
            className={`mfx-slot ${slot.bypassed ? "mfx-slot--bypassed" : ""}`}
          >
            <span className="mfx-slot-index">{slot.slotIndex}</span>
            <span className="mfx-slot-type">{labelFor(slot.fxType)}</span>
            {(PARAM_CONFIGS[slot.fxType] ?? []).map((p) => (
              <label key={p.key} className="mfx-param">
                <span className="mfx-param-label">{p.label}</span>
                {p.isText ? (
                  <input
                    className="mfx-param-input"
                    type="text"
                    value={String(slot.params[p.key] ?? "")}
                    onChange={(e) => setParam(slot.slotIndex, p.key, e.target.value)}
                  />
                ) : (
                  <>
                    <input
                      className="mfx-param-slider"
                      type="range"
                      min={p.min}
                      max={p.max}
                      step={p.step}
                      value={Number(slot.params[p.key] ?? p.min)}
                      onChange={(e) => setParam(slot.slotIndex, p.key, parseFloat(e.target.value))}
                    />
                    <span className="mfx-param-value">{Number(slot.params[p.key] ?? p.min).toFixed(p.step < 1 ? (p.step < 0.01 ? 3 : 2) : 0)}</span>
                  </>
                )}
              </label>
            ))}
            <button className="mfx-btn" onClick={() => toggleBypass(slot)} title="Toggle bypass">
              Byp
            </button>
            <button className="mfx-btn" onClick={() => removeSlot(slot.slotIndex)} title="Remove">
              Del
            </button>
          </div>
        ))}
      </div>
    </div>
  );
}
