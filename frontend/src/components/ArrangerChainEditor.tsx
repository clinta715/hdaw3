import React, { useCallback, useState } from "react";
import {
  useArrangerStore,
  ArrangerRegionSnapshot,
  ArrangerChainSnapshot,
} from "../store/arrangerStore";
import { rpc } from "../rpc";
import { useNotifyStore, reportRpcError } from "../store/notifyStore";
import "./ArrangerChainEditor.css";

export const ArrangerChainEditor: React.FC = () => {
  const regions = useArrangerStore((s) => s.regions);
  const chains = useArrangerStore((s) => s.chains);
  const [dragEntryIdx, setDragEntryIdx] = useState<number | null>(null);

  const activeChain = chains.find((c) => c.isActive) || chains[0];
  const regionMap = new Map(regions.map((r) => [r.regionID, r]));

  const handleNewChain = useCallback(async () => {
    const name = `Arrangement ${String.fromCharCode(65 + chains.length)}`;
    await rpc.call("project.addArrangerChain", { name });
  }, [chains.length]);

  const handleDeleteChain = useCallback(async () => {
    if (activeChain) {
      await rpc.call("project.removeArrangerChain", { chainID: activeChain.chainID });
    }
  }, [activeChain]);

  const handleSwitchChain = useCallback(async (chainID: string) => {
    await rpc.call("project.setArrangerChainActive", { chainID });
  }, []);

  const handleAddEntry = useCallback(
    async (regionID: string) => {
      if (!activeChain) return;
      await rpc.call("project.addChainEntry", {
        chainID: activeChain.chainID,
        regionID,
        repeatCount: 1,
      });
    },
    [activeChain]
  );

  const handleRemoveEntry = useCallback(
    async (entryIndex: number) => {
      if (!activeChain) return;
      await rpc.call("project.removeChainEntry", {
        chainID: activeChain.chainID,
        entryIndex,
      });
    },
    [activeChain]
  );

  const handleSetRepeat = useCallback(
    async (entryIndex: number, currentRepeat: number) => {
      if (!activeChain) return;
      const newRepeat = currentRepeat >= 8 ? 1 : currentRepeat + 1;
      await rpc.call("project.setChainEntryRepeat", {
        chainID: activeChain.chainID,
        entryIndex,
        repeatCount: newRepeat,
      });
    },
    [activeChain]
  );

  const handleDragStart = useCallback((idx: number) => {
    setDragEntryIdx(idx);
  }, []);

  const handleDragOver = useCallback(
    async (e: React.DragEvent, idx: number) => {
      e.preventDefault();
      if (dragEntryIdx === null || !activeChain || dragEntryIdx === idx) return;
      await rpc.call("project.reorderChainEntry", {
        chainID: activeChain.chainID,
        fromIndex: dragEntryIdx,
        toIndex: idx,
      });
      setDragEntryIdx(idx);
    },
    [dragEntryIdx, activeChain]
  );

  const handleDragEnd = useCallback(() => {
    setDragEntryIdx(null);
  }, []);

  const canFlatten = !!activeChain && activeChain.entries.length > 0;

  const handleFlatten = useCallback(async () => {
    try {
      await rpc.call("project.flattenArranger", {});
      useNotifyStore.getState().push({ level: "success", message: "Arranger flattened to timeline" });
    } catch (err) {
      reportRpcError("project.flattenArranger", err);
    }
  }, []);

  return (
    <div className="arranger-chain-editor">
      <div className="arranger-chain-toolbar">
        <span style={{ fontSize: "12px", fontWeight: 600 }}>Chain:</span>
        <select
          value={activeChain?.chainID || ""}
          onChange={(e) => handleSwitchChain(e.target.value)}
        >
          {chains.map((c) => (
            <option key={c.chainID} value={c.chainID}>
              {c.name}{c.isActive ? " *" : ""}
            </option>
          ))}
        </select>
        <button onClick={handleNewChain}>+ New</button>
        <button onClick={handleDeleteChain} disabled={!activeChain || chains.length <= 1}>
          Delete
        </button>
        <button
          className="arranger-chain-flatten"
          onClick={handleFlatten}
          disabled={!canFlatten}
          title="Expand active chain into real clips on the timeline"
        >
          Flatten
        </button>
        <div style={{ flex: 1 }} />
      </div>

      <div className="arranger-chain-columns">
        <div className="arranger-chain-column">
          <div className="arranger-chain-column-header">
            Active Chain ({activeChain?.entries.length || 0} entries)
          </div>
          <div className="arranger-chain-list">
            {activeChain?.entries.map((entry, idx) => {
              const region = regionMap.get(entry.regionID);
              if (!region) return null;
              return (
                <div
                  key={`${entry.regionID}-${idx}`}
                  className={`arranger-chain-entry${dragEntryIdx === idx ? " dragging" : ""}`}
                  draggable
                  onDragStart={() => handleDragStart(idx)}
                  onDragOver={(e) => handleDragOver(e, idx)}
                  onDragEnd={handleDragEnd}
                >
                  <span style={{ color: "var(--text-secondary)", fontSize: "11px", minWidth: 20 }}>
                    {idx + 1}.
                  </span>
                  <span className="arranger-chain-entry-name">{region.name}</span>
                  <span
                    className="arranger-chain-entry-repeat"
                    onClick={() => handleSetRepeat(idx, entry.repeatCount)}
                    title="Click to cycle repeat count"
                  >
                    x{entry.repeatCount}
                  </span>
                  <button
                    className="arranger-chain-entry-remove"
                    onClick={() => handleRemoveEntry(idx)}
                    title="Remove from chain"
                  >
                    x
                  </button>
                </div>
              );
            })}
            {(!activeChain || activeChain.entries.length === 0) && (
              <div style={{ padding: "12px", color: "var(--text-secondary)", fontSize: "12px", textAlign: "center" }}>
                Double-click a region on the right to add it
              </div>
            )}
          </div>
        </div>

        <div className="arranger-chain-column">
          <div className="arranger-chain-column-header">
            Available Regions ({regions.length})
          </div>
          <div className="arranger-chain-list">
            {regions.map((region) => (
              <div
                key={region.regionID}
                className="arranger-region-item"
                onDoubleClick={() => handleAddEntry(region.regionID)}
                title="Double-click to add to chain"
              >
                <span>{region.name}</span>
                <span className="arranger-region-item-beats">
                  {region.startTime.toFixed(1)}-{(region.startTime + region.duration).toFixed(1)}
                </span>
              </div>
            ))}
            {regions.length === 0 && (
              <div style={{ padding: "12px", color: "var(--text-secondary)", fontSize: "12px", textAlign: "center" }}>
                Draw regions on the timeline arranger lane
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
