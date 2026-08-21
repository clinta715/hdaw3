import React, { useState, useCallback, useEffect, useRef } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useTransportStore } from "../store/transportStore";
import type { PoolEntry } from "../rpc/types";
import "./PoolView.css";

export default React.memo(function PoolView() {
  const [entries, setEntries] = useState<PoolEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [filter, setFilter] = useState("");
  const [isPlaying, setIsPlaying] = useState(false);
  const [previewFile, setPreviewFile] = useState<{ sourceFile: string; name: string } | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

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

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (pollRef.current) clearInterval(pollRef.current);
      rpc.call("preview.stop").catch(() => {});
    };
  }, []);

  const stopPreview = useCallback(async () => {
    await rpc.call("preview.stop").catch(() => {});
    setIsPlaying(false);
    if (pollRef.current) {
      clearInterval(pollRef.current);
      pollRef.current = null;
    }
  }, []);

  const playPreview = useCallback(async (sourceFile: string, name: string) => {
    await stopPreview();
    try {
      await rpc.call("preview.load", { filePath: sourceFile });
      await rpc.call("preview.play");
    } catch {
      setIsPlaying(false);
      return;
    }
    setIsPlaying(true);
    setPreviewFile({ sourceFile, name });

    if (pollRef.current) clearInterval(pollRef.current);
    pollRef.current = setInterval(async () => {
      try {
        const playing = await rpc.call("preview.isPlaying") as boolean;
        if (!playing) {
          setIsPlaying(false);
          if (pollRef.current) {
            clearInterval(pollRef.current);
            pollRef.current = null;
          }
        }
      } catch {
        if (pollRef.current) {
          clearInterval(pollRef.current);
          pollRef.current = null;
        }
        setIsPlaying(false);
      }
    }, 500);
  }, [stopPreview]);

  const handlePreviewEntry = useCallback(async (sourceFile: string, name: string) => {
    if (isPlaying && previewFile?.sourceFile === sourceFile) {
      await stopPreview();
    } else {
      await playPreview(sourceFile, name);
    }
  }, [isPlaying, previewFile, playPreview, stopPreview]);

  const handleDragStart = useCallback((e: React.DragEvent, entry: PoolEntry) => {
    e.dataTransfer.setData("application/hdaw-file", JSON.stringify({ path: entry.sourceFile, name: entry.name }));
    e.dataTransfer.effectAllowed = "copy";
  }, []);

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
        {filtered.map((entry, i) => {
          const isUnused = entry.usageCount === 0;
          const isPreviewing = isPlaying && previewFile?.sourceFile === entry.sourceFile;
          return (
            <div
              key={`${entry.sourceFile}-${i}`}
              className={`pool-view-item${isUnused ? " pool-view-item--unused" : ""}${isPreviewing ? " pool-view-item--previewing" : ""}`}
              title={entry.sourceFile}
              draggable
              onDragStart={(e) => handleDragStart(e, entry)}
              onClick={() => handlePreviewEntry(entry.sourceFile, entry.name)}
            >
              <span className="pool-col-name">
                {isPreviewing && <span className="pool-preview-icon">&#9632;</span>}
                {!isPreviewing && <span className="pool-preview-icon pool-preview-icon--play">&#9654;</span>}
                {entry.name || "(untitled)"}
              </span>
              <span className="pool-col-usage">
                {entry.usageCount}
                {isUnused && <span className="pool-unused-badge">unused</span>}
              </span>
              <span className="pool-col-duration">{formatDuration(entry.duration)}</span>
              <span className="pool-col-rate">{entry.sampleRate ? `${(entry.sampleRate / 1000).toFixed(1)}k` : "\u2014"}</span>
              <span className="pool-col-ch">{entry.channels || "\u2014"}</span>
            </div>
          );
        })}
      </div>
      <div className="pool-view-footer">
        <span>{entries.length} file{entries.length !== 1 ? "s" : ""}</span>
      </div>
    </div>
  );
});
