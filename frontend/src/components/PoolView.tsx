import React, { useState, useCallback, useEffect } from "react";
import { rpc } from "../rpc";
import type { PoolEntry } from "../rpc/types";
import "./PoolView.css";

export default React.memo(function PoolView() {
  const [entries, setEntries] = useState<PoolEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [filter, setFilter] = useState("");

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await rpc.call("pool.list", {});
      setEntries(Array.isArray(res) ? res : []);
    } catch {
      setEntries([]);
    }
    setLoading(false);
  }, []);

  useEffect(() => { refresh(); }, [refresh]);

  const filtered = filter.trim()
    ? entries.filter(e =>
        e.name.toLowerCase().includes(filter.toLowerCase()) ||
        e.sourceFile.toLowerCase().includes(filter.toLowerCase())
      )
    : entries;

  const formatDuration = (sec: number) => {
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m}:${s.toString().padStart(2, "0")}`;
  };

  return (
    <div className="pool-view">
      <div className="pool-view-toolbar">
        <input
          className="pool-view-search"
          type="text"
          placeholder="Filter files..."
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
        />
        <button className="pool-view-refresh" onClick={refresh} title="Refresh">
          ↻
        </button>
        {loading && <span className="pool-view-status">Loading...</span>}
      </div>
      <div className="pool-view-list">
        <div className="pool-view-header">
          <span className="pool-col-name">Name</span>
          <span className="pool-col-usage">Uses</span>
          <span className="pool-col-duration">Duration</span>
          <span className="pool-col-rate">Rate</span>
          <span className="pool-col-ch">Ch</span>
        </div>
        {filtered.length === 0 && !loading && (
          <div className="pool-view-empty">
            {entries.length === 0 ? "No audio files in project" : "No matches"}
          </div>
        )}
        {filtered.map((entry, i) => (
          <div
            key={`${entry.sourceFile}-${i}`}
            className="pool-view-item"
            title={entry.sourceFile}
          >
            <span className="pool-col-name">{entry.name || "(untitled)"}</span>
            <span className="pool-col-usage">{entry.usageCount}</span>
            <span className="pool-col-duration">{formatDuration(entry.duration)}</span>
            <span className="pool-col-rate">{entry.sampleRate ? `${(entry.sampleRate / 1000).toFixed(1)}k` : "\u2014"}</span>
            <span className="pool-col-ch">{entry.channels || "\u2014"}</span>
          </div>
        ))}
      </div>
      <div className="pool-view-footer">
        <span>{entries.length} file{entries.length !== 1 ? "s" : ""}</span>
      </div>
    </div>
  );
});
