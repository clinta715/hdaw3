import React, { useState, useCallback, useEffect, useRef } from "react";
import { rpc } from "../rpc";
import type { PresetSearchResult, FxSlotSnapshot } from "../rpc/types";
import { useUiStore } from "../store/uiStore";
import "./PresetBrowser.css";

export default React.memo(function PresetBrowser() {
  const [query, setQuery] = useState("");
  const [results, setResults] = useState<PresetSearchResult[]>([]);
  const [loading, setLoading] = useState(false);
  const [loadedMsg, setLoadedMsg] = useState<string | null>(null);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);

  const doSearch = useCallback(async (q: string) => {
    if (q.trim().length < 2) {
      setResults([]);
      return;
    }
    setLoading(true);
    try {
      const res = await rpc.call("plugin.searchPresets", { query: q, limit: 100 });
      setResults(Array.isArray(res) ? res : []);
    } catch {
      setResults([]);
    }
    setLoading(false);
  }, []);

  useEffect(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => doSearch(query), 250);
    return () => { if (debounceRef.current) clearTimeout(debounceRef.current); };
  }, [query, doSearch]);

  const handleLoad = useCallback(async (preset: PresetSearchResult) => {
    const trackIndex = useUiStore.getState().selectedTrackIndex;
    if (trackIndex == null) {
      setLoadedMsg("Select a track first");
      setTimeout(() => setLoadedMsg(null), 2000);
      return;
    }

    let slots: FxSlotSnapshot[];
    try {
      const data = await rpc.call("read.getFxSlots", { trackIndex });
      slots = Array.isArray(data) ? data as FxSlotSnapshot[] : [];
    } catch {
      setLoadedMsg("Failed to read FX slots");
      setTimeout(() => setLoadedMsg(null), 2000);
      return;
    }

    const slot = slots.find((s) => s.fxType === "plugin" && s.pluginId === preset.pluginId);
    if (!slot) {
      setLoadedMsg(`No ${preset.pluginName} slot on this track`);
      setTimeout(() => setLoadedMsg(null), 2000);
      return;
    }

    try {
      await rpc.call("pluginParam.setCurrentProgram", {
        trackIndex,
        pluginID: preset.pluginId,
        programIndex: preset.presetIndex,
      });
      setLoadedMsg(`Loaded: ${preset.presetName}`);
      setTimeout(() => setLoadedMsg(null), 2000);
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : "failed";
      setLoadedMsg(`Error: ${msg}`);
      setTimeout(() => setLoadedMsg(null), 3000);
    }
  }, []);

  return (
    <div className="preset-browser">
      <div className="preset-browser-toolbar">
        <input
          className="preset-browser-search"
          type="text"
          placeholder="Search presets..."
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          autoFocus
        />
        {loading && <span className="preset-browser-status">Searching...</span>}
        {loadedMsg && <span className="preset-browser-status">{loadedMsg}</span>}
      </div>
      <div className="preset-browser-results">
        {results.length === 0 && query.trim().length >= 2 && !loading && (
          <div className="preset-browser-empty">No presets found</div>
        )}
        {results.map((r, i) => (
          <div
            key={`${r.pluginId}-${r.presetIndex}-${i}`}
            className="preset-browser-item"
            onClick={() => handleLoad(r)}
            title={`Load "${r.presetName}" from ${r.pluginName}`}
          >
            <span className="preset-browser-item-name">{r.presetName}</span>
            <span className="preset-browser-item-plugin">{r.pluginName}</span>
          </div>
        ))}
      </div>
    </div>
  );
});
