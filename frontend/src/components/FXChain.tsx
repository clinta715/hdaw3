import { useState, useEffect, useCallback, useRef } from "react";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { FxSlotSnapshot } from "../rpc/types";
import "./FXChain.css";

interface PluginInfo {
  name: string;
  format: string;
  manufacturer: string;
  fileOrIdentifier: string;
  isInstrument: boolean;
}

interface ParamInfo {
  paramIndex: number;
  name: string;
  value: number;
  text: string;
  label: string;
  automatable: boolean;
}

const INTERNAL_FX = [
  { label: "EQ", fxType: "eq" },
  { label: "Compressor", fxType: "compressor" },
  { label: "Reverb", fxType: "reverb" },
  { label: "Delay", fxType: "delay" },
];

export default function FXChain() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [slots, setSlots] = useState<FxSlotSnapshot[]>([]);
  const [refreshKey, setRefreshKey] = useState(0);
  const [menuOpen, setMenuOpen] = useState(false);
  const [instruments, setInstruments] = useState<PluginInfo[]>([]);
  const [effects, setEffects] = useState<PluginInfo[]>([]);
  const [expandedParams, setExpandedParams] = useState<Set<number>>(new Set());
  const [slotParams, setSlotParams] = useState<Map<number, ParamInfo[]>>(new Map());
  const [dragSlot, setDragSlot] = useState<number | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);

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

  const fetchPluginLists = useCallback(() => {
    rpc.call("plugin.getInstrumentPlugins", {})
      .then((data) => { if (Array.isArray(data)) setInstruments(data as PluginInfo[]); })
      .catch(() => {});
    rpc.call("plugin.getEffectPlugins", {})
      .then((data) => { if (Array.isArray(data)) setEffects(data as PluginInfo[]); })
      .catch(() => {});
  }, []);

  useEffect(() => {
    fetchPluginLists();
    // Re-fetch when a plugin scan completes. The startup scan (empty cache)
    // and any manual/directory-watcher-triggered rescans broadcast
    // notify.scanProgress with done=true at completion. Without this, the
    // FX dropdown's Instruments/Effects sections would stay empty if the
    // FX panel was mounted before the scan finished — even though the
    // Plugin Manager (which does subscribe) would show the plugins fine.
    const unsub = rpc.onNotification("notify.scanProgress", (_method, params) => {
      const p = params as Record<string, unknown>;
      if (p.done === true) fetchPluginLists();
    });
    return unsub;
  }, [fetchPluginLists]);

  useEffect(() => {
    if (!menuOpen) return;
    const handleClick = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        setMenuOpen(false);
      }
    };
    document.addEventListener("mousedown", handleClick);
    return () => document.removeEventListener("mousedown", handleClick);
  }, [menuOpen]);

  const refresh = useCallback(() => setRefreshKey(k => k + 1), []);

  const addSlot = useCallback(async (fxType: string, pluginId: string = "") => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.addFxSlot", {
        trackIndex: selectedTrackIndex,
        fxType,
        slotIndex: -1,
        pluginId,
      });
      refresh();
    } catch (e) { console.error("addFxSlot failed", e); }
  }, [selectedTrackIndex, refresh]);

  const removeSlot = useCallback(async (slotIndex: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.removeFxSlot", { trackIndex: selectedTrackIndex, slotIndex });
      refresh();
    } catch (e) { console.error("removeFxSlot failed", e); }
  }, [selectedTrackIndex, refresh]);

  const toggleBypass = useCallback(async (slotIndex: number, currentlyBypassed: boolean) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.setFxSlotBypassed", { trackIndex: selectedTrackIndex, slotIndex, bypassed: !currentlyBypassed });
      refresh();
    } catch (e) { console.error("setFxSlotBypassed failed", e); }
  }, [selectedTrackIndex, refresh]);

  const toggleEditor = useCallback(async (slotIndex: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("audioGraph.toggleFXEditor", { trackIndex: selectedTrackIndex, slotIndex });
    } catch (e) { console.error("toggleFXEditor failed", e); }
  }, [selectedTrackIndex]);

  const moveSlot = useCallback(async (slotIndex: number, direction: -1 | 1) => {
    if (selectedTrackIndex == null) return;
    const toSlot = slotIndex + direction;
    if (toSlot < 0 || toSlot >= slots.length) return;
    try {
      await rpc.call("project.reorderFxSlots", { trackIndex: selectedTrackIndex, fromSlot: slotIndex, toSlot });
      refresh();
    } catch (e) { console.error("reorderFxSlots failed", e); }
  }, [selectedTrackIndex, slots.length, refresh]);

  const fetchParams = useCallback(async (slot: FxSlotSnapshot) => {
    if (selectedTrackIndex == null || !slot.pluginId) return;
    try {
      const data = await rpc.call("pluginParam.getParams", { trackIndex: selectedTrackIndex, pluginID: slot.pluginId });
      if (Array.isArray(data)) {
        setSlotParams(prev => new Map(prev).set(slot.slotIndex, data as ParamInfo[]));
      }
    } catch (e) { console.error("getParams failed", e); }
  }, [selectedTrackIndex]);

  const toggleParams = useCallback((slot: FxSlotSnapshot) => {
    setExpandedParams(prev => {
      const next = new Set(prev);
      if (next.has(slot.slotIndex)) {
        next.delete(slot.slotIndex);
      } else {
        next.add(slot.slotIndex);
        if (!slotParams.has(slot.slotIndex)) fetchParams(slot);
      }
      return next;
    });
  }, [slotParams, fetchParams]);

  const setParam = useCallback(async (pluginID: string, paramIndex: number, value: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("pluginParam.setParam", { trackIndex: selectedTrackIndex, pluginID, paramIndex, normalizedValue: value });
    } catch (e) { console.error("setParam failed", e); }
  }, [selectedTrackIndex]);

  const handleDragStart = useCallback((e: React.DragEvent, slotIndex: number) => {
    setDragSlot(slotIndex);
    e.dataTransfer.effectAllowed = "move";
  }, []);

  const handleDragOver = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = "move";
  }, []);

  const handleDrop = useCallback(async (e: React.DragEvent, targetSlotIndex: number) => {
    e.preventDefault();
    if (selectedTrackIndex == null || dragSlot == null || dragSlot === targetSlotIndex) {
      setDragSlot(null);
      return;
    }
    try {
      await rpc.call("project.reorderFxSlots", { trackIndex: selectedTrackIndex, fromSlot: dragSlot, toSlot: targetSlotIndex });
      refresh();
    } catch (err) { console.error("reorderFxSlots failed", err); }
    setDragSlot(null);
  }, [selectedTrackIndex, dragSlot, refresh]);

  const handleDragEnd = useCallback(() => setDragSlot(null), []);

  if (selectedTrackIndex == null) {
    return <div className="fx-chain"><div className="fx-empty">Select a track to edit FX</div></div>;
  }

  return (
    <div className="fx-chain">
      <div className="fx-header">
        <span className="fx-title">FX Chain — Track {selectedTrackIndex}</span>
        <div className="fx-add-menu-container" ref={menuRef}>
          <button className="fx-add-btn" onClick={() => setMenuOpen(o => !o)}>+ Add FX</button>
          {menuOpen && (
            <div className="fx-dropdown">
              <div className="fx-dropdown-section">
                <div className="fx-dropdown-label">Internal</div>
                {INTERNAL_FX.map(fx => (
                  <button
                    key={fx.fxType}
                    className="fx-dropdown-item"
                    onClick={() => { addSlot(fx.fxType); setMenuOpen(false); }}
                  >{fx.label}</button>
                ))}
              </div>
              {instruments.length > 0 && (
                <div className="fx-dropdown-section">
                  <div className="fx-dropdown-label">Instruments</div>
                  {instruments.map(p => (
                    <button
                      key={p.fileOrIdentifier}
                      className="fx-dropdown-item"
                      onClick={() => { addSlot("plugin", p.fileOrIdentifier); setMenuOpen(false); }}
                    >{p.name} <span className="fx-dropdown-manufacturer">{p.manufacturer}</span></button>
                  ))}
                </div>
              )}
              {effects.length > 0 && (
                <div className="fx-dropdown-section">
                  <div className="fx-dropdown-label">Effects</div>
                  {effects.map(p => (
                    <button
                      key={p.fileOrIdentifier}
                      className="fx-dropdown-item"
                      onClick={() => { addSlot("plugin", p.fileOrIdentifier); setMenuOpen(false); }}
                    >{p.name} <span className="fx-dropdown-manufacturer">{p.manufacturer}</span></button>
                  ))}
                </div>
              )}
            </div>
          )}
        </div>
      </div>
      {slots.length === 0 && (
        <div className="fx-empty-slots">No FX slots. Click + Add FX to create one.</div>
      )}
      {slots.map((slot) => (
        <div
          key={slot.slotIndex}
          className={`fx-slot${slot.bypassed ? " fx-slot--bypassed" : ""}${dragSlot === slot.slotIndex ? " fx-slot--dragging" : ""}`}
          draggable
          onDragStart={(e) => handleDragStart(e, slot.slotIndex)}
          onDragOver={handleDragOver}
          onDrop={(e) => handleDrop(e, slot.slotIndex)}
          onDragEnd={handleDragEnd}
        >
          <div className="fx-slot-main">
            <span className="fx-slot-idx">{slot.slotIndex}</span>
            <span className="fx-slot-type">{slot.fxType}</span>
            <span className="fx-slot-name">{slot.pluginName || `Slot ${slot.slotIndex}`}</span>
            <button
              className="fx-btn fx-reorder"
              onClick={() => moveSlot(slot.slotIndex, -1)}
              disabled={slot.slotIndex === 0}
              title="Move up"
            >&#9650;</button>
            <button
              className="fx-btn fx-reorder"
              onClick={() => moveSlot(slot.slotIndex, 1)}
              disabled={slot.slotIndex === slots.length - 1}
              title="Move down"
            >&#9660;</button>
            <button
              className="fx-btn fx-edit"
              onClick={() => toggleEditor(slot.slotIndex)}
              title="Edit plugin"
            >&#9998;</button>
            <button
              className={`fx-btn fx-bypass${slot.bypassed ? " active" : ""}`}
              onClick={() => toggleBypass(slot.slotIndex, slot.bypassed)}
              title="Bypass"
            >B</button>
            <button className="fx-btn fx-remove" onClick={() => removeSlot(slot.slotIndex)} title="Remove">✕</button>
          </div>
          {slot.paramCount > 0 && (
            <div className="fx-params-toggle" onClick={() => toggleParams(slot)}>
              {expandedParams.has(slot.slotIndex) ? "▼" : "▶"} Parameters ({slot.paramCount})
            </div>
          )}
          {expandedParams.has(slot.slotIndex) && slotParams.has(slot.slotIndex) && (
            <div className="fx-params-list">
              {(slotParams.get(slot.slotIndex) || []).map((param) => (
                <div key={param.paramIndex} className="fx-param-row">
                  <span className="fx-param-name" title={param.name}>{param.name}</span>
                  <input
                    type="range"
                    className="fx-param-slider"
                    min={0}
                    max={1000}
                    value={Math.round(param.value * 1000)}
                    onChange={(e) => {
                      const v = Number(e.target.value) / 1000;
                      setParam(slot.pluginId, param.paramIndex, v);
                      setSlotParams(prev => {
                        const next = new Map(prev);
                        const arr = [...(next.get(slot.slotIndex) || [])];
                        // Find by paramIndex, not by array position: VST3 plugins
                        // commonly report non-contiguous or non-zero-based
                        // paramIndex values (hidden params, bypass slots, etc.),
                        // so arr[param.paramIndex] would write to the wrong row
                        // or create sparse holes.
                        const idx = arr.findIndex((p) => p.paramIndex === param.paramIndex);
                        if (idx >= 0) {
                          arr[idx] = { ...arr[idx], value: v };
                        }
                        next.set(slot.slotIndex, arr);
                        return next;
                      });
                    }}
                  />
                  <span className="fx-param-value">{param.text || `${(param.value * 100).toFixed(0)}%`}</span>
                </div>
              ))}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}
