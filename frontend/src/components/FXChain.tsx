import { useState, useEffect, useCallback } from "react";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { FxSlotSnapshot } from "../rpc/types";
import "./FXChain.css";

export default function FXChain() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [slots, setSlots] = useState<FxSlotSnapshot[]>([]);
  const [refreshKey, setRefreshKey] = useState(0);

  useEffect(() => {
    if (selectedTrackIndex == null) {
      setSlots([]);
      return;
    }
    rpc.call("read.getFxSlots", { trackIndex: selectedTrackIndex })
      .then((data) => {
        if (Array.isArray(data)) setSlots(data as FxSlotSnapshot[]);
        else setSlots([]);
      })
      .catch(() => setSlots([]));
  }, [selectedTrackIndex, refreshKey]);

  const addSlot = useCallback(async () => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.addFxSlot", {
        trackIndex: selectedTrackIndex,
        fxType: "internal",
        slotIndex: -1,
        pluginId: ""
      });
      setRefreshKey(k => k + 1);
    } catch (e) { console.error("addFxSlot failed", e); }
  }, [selectedTrackIndex]);

  const removeSlot = useCallback(async (slotIndex: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.removeFxSlot", { trackIndex: selectedTrackIndex, slotIndex });
      setRefreshKey(k => k + 1);
    } catch (e) { console.error("removeFxSlot failed", e); }
  }, [selectedTrackIndex]);

  const toggleBypass = useCallback(async (slotIndex: number, currentlyBypassed: boolean) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.setFxSlotBypassed", { trackIndex: selectedTrackIndex, slotIndex, bypassed: !currentlyBypassed });
      setRefreshKey(k => k + 1);
    } catch (e) { console.error("setFxSlotBypassed failed", e); }
  }, [selectedTrackIndex]);

  if (selectedTrackIndex == null) {
    return <div className="fx-chain"><div className="fx-empty">Select a track to edit FX</div></div>;
  }

  return (
    <div className="fx-chain">
      <div className="fx-header">
        <span className="fx-title">FX Chain — Track {selectedTrackIndex}</span>
        <button className="fx-add-btn" onClick={addSlot}>+ Add Internal</button>
      </div>
      {slots.length === 0 && (
        <div className="fx-empty-slots">No FX slots. Click + Add to create one.</div>
      )}
      {slots.map((slot) => (
        <div key={slot.slotIndex} className={`fx-slot${slot.bypassed ? " fx-slot--bypassed" : ""}`}>
          <span className="fx-slot-idx">{slot.slotIndex}</span>
          <span className="fx-slot-type">{slot.fxType}</span>
          <span className="fx-slot-name">{slot.pluginName || `Slot ${slot.slotIndex}`}</span>
          <button
            className={`fx-btn fx-bypass${slot.bypassed ? " active" : ""}`}
            onClick={() => toggleBypass(slot.slotIndex, slot.bypassed)}
          >B</button>
          <button className="fx-btn fx-remove" onClick={() => removeSlot(slot.slotIndex)}>✕</button>
        </div>
      ))}
    </div>
  );
}
