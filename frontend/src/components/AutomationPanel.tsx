import { useEffect, useState } from "react";
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
  const selectedClipId = useUiStore((s) => s.selectedClipId);
  const [automatableParams, setAutomatableParams] = useState<AutomatableParamSnapshot[]>([]);
  const { lanes, pointsByLane, activeTrackIndex, loading, fetchForTrack } = useAutomationStore();

  // Resolve trackIndex from selected clip. This assumes the store has a way to look up
  // the parent track or that the backend returns trackIndex on the clip.
  const clipTrackIndex = selectedClipId != null ? selectedClipId : null;

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

  if (clipTrackIndex === null) {
    return (
      <div className="automation-panel">
        <div className="ap-empty">Select a clip to edit automation lanes</div>
      </div>
    );
  }

  if (loading) {
    return (
      <div className="automation-panel">
        <div className="ap-empty">Loading...</div>
      </div>
    );
  }

  return (
    <div className="automation-panel">
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
            paramID={lane.paramID}
          />
        ))}
      </div>
    </div>
  );
}
