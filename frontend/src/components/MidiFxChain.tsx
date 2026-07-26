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
];

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
