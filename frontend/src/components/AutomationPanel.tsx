import { useEffect, useState, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { useUiStore } from "../store/uiStore";
import { useAutomationStore } from "../store/automationStore";
import { AutomatableParamSnapshot } from "../rpc/types";
import AutomationLaneCanvas from "./AutomationLaneCanvas";
import "./AutomationPanel.css";

interface Props {
  rpc: RpcClient;
}

export default function AutomationPanel({ rpc }: Props) {
  const { selectedClipId, selectedTrackIndex } = useUiStore((s) => ({ selectedClipId: s.selectedClipId, selectedTrackIndex: s.selectedTrackIndex }));
  const [automatableParams, setAutomatableParams] = useState<AutomatableParamSnapshot[]>([]);
  const { lanes, pointsByLane, activeTrackIndex, loading, fetchForTrack } = useAutomationStore();

  // Resolve trackIndex from selected clip
  const clipTrackIndex = selectedTrackIndex;

  useEffect(() => {
    if (clipTrackIndex === null) {
      // No clip selected → show master automation or nothing
      return;
    }
    // Fetch lanes and automatable params in parallel
    fetchForTrack(clipTrackIndex, rpc);
    rpc.call("read.getAutomatableParams", { trackIndex: clipTrackIndex })
      .then((params) => setAutomatableParams(params as AutomatableParamSnapshot[]))
      .catch(() => {}); // ignore fetch errors for the param list
  }, [clipTrackIndex, rpc, fetchForTrack]);

  const handleAddLane = async () => {
    if (activeTrackIndex === null) return;
    // Pick first un-added automatable param
    const existing = new Set(lanes.map((l) => l.paramID));
    const next = automatableParams.find((p) => !existing.has(p.paramIndex));
    if (!next) return;
    try {
      await rpc.call("project.addAutomationLane", {
        trackIndex: activeTrackIndex,
        paramID: next.paramIndex,
      });
      await fetchForTrack(activeTrackIndex, rpc);
    } catch (err) {
      console.error("Add lane failed:", err);
    }
  };

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    const { lanes, selectedPointTimes, activeTrackIndex, removePoints, selectAll, clearSelection } = useAutomationStore.getState();
    if (activeTrackIndex === null) return;

    for (const lane of lanes) {
      const sel = selectedPointTimes.get(lane.name);
      if (sel && sel.size > 0) {
        if (e.key === "Delete" || e.key === "Backspace") {
          e.preventDefault();
          removePoints(activeTrackIndex, lane.name, [...sel], rpc);
          clearSelection(lane.name);
        } else if (e.key === "a" && (e.ctrlKey || e.metaKey)) {
          e.preventDefault();
          selectAll(lane.name);
        } else if (e.key === "Escape") {
          clearSelection(lane.name);
        }
        break;
      }
    }
  }, [rpc]);

  if (clipTrackIndex === null) {
    return (
      <div className="automation-panel" tabIndex={0} onKeyDown={handleKeyDown}>
        <div className="ap-empty">Select a clip to edit automation lanes</div>
      </div>
    );
  }

  if (loading) {
    return (
      <div className="automation-panel" tabIndex={0} onKeyDown={handleKeyDown}>
        <div className="ap-empty">Loading...</div>
      </div>
    );
  }

  return (
    <div className="automation-panel" tabIndex={0} onKeyDown={handleKeyDown}>
      <div className="ap-toolbar">
        <button className="ap-add-btn" onClick={handleAddLane} disabled={lanes.length >= automatableParams.length}>
          + Add Lane
        </button>
        <span className="ap-track-label">Track {activeTrackIndex}</span>
      </div>
      <div className="ap-canvas-list">
        {lanes.length === 0 && (
          <div className="ap-empty-no-lanes">No automation lanes. Click + Add Lane to create one.</div>
        )}
        {lanes.map((lane) => (
          <AutomationLaneCanvas
            key={lane.laneIndex}
            laneName={lane.name}
            points={pointsByLane.get(lane.name) ?? []}
            trackIndex={activeTrackIndex!}
            rpc={rpc}
            viewStartBeat={0}
            viewEndBeat={32}
          />
        ))}
      </div>
    </div>
  );
}
