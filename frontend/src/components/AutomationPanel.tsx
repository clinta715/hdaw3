import { useEffect, useState, useCallback, useRef } from "react";
import { useShallow } from "zustand/react/shallow";
import { RpcClient } from "../rpc/client";
import { useUiStore } from "../store/uiStore";
import { useAutomationStore } from "../store/automationStore";
import { AutomatableParamSnapshot } from "../rpc/types";
import AutomationLaneCanvas from "./AutomationLaneCanvas";
import EnvelopeGenerateControl from "./EnvelopeGenerateControl";
import "./AutomationPanel.css";

const PARAM_NAMES: Record<number, string> = {
  1: "Volume",
  2: "Pan",
  3: "Mute",
};

// Engine pid composition. getAutomatableParams snapshots carry a BARE
// paramIndex for audio FX entries (compose 100 + slotIndex*100 + paramIndex,
// range 100..999) and the FULL pid in paramIndex (>= 1000) for MIDI-FX
// entries (pass through unchanged).
function composePid(slotIndex: number, paramIndex: number): number {
  return paramIndex >= 1000 ? paramIndex : 100 + slotIndex * 100 + paramIndex;
}

interface Props {
  rpc: RpcClient;
}

export default function AutomationPanel({ rpc }: Props) {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [automatableParams, setAutomatableParams] = useState<AutomatableParamSnapshot[]>([]);
  const addLaneSelectRef = useRef<HTMLSelectElement>(null);
  const { lanes, pointsByLane, activeTrackIndex, loading, pinnedLaneParamID } = useAutomationStore(
    useShallow((s) => ({ lanes: s.lanes, pointsByLane: s.pointsByLane, activeTrackIndex: s.activeTrackIndex, loading: s.loading, pinnedLaneParamID: s.pinnedLaneParamID }))
  );
  const fetchForTrack = useAutomationStore((s) => s.fetchForTrack);
  const removeLane = useAutomationStore((s) => s.removeLane);
  const setPinnedLaneParamID = useAutomationStore((s) => s.setPinnedLaneParamID);
  const mountedRef = useRef(false);

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

  // Joker effect: when unpinned, follow lastClickedParamID to switch primary lane.
  // Reads lastClickedParamID/pinnedLaneParamID from getState() inside the
  // effect so the dependency array is stable — avoids a re-render cascade
  // where lanes→useShallow→re-render→effect re-fires.
  const lastClickedRef = useRef<number | null>(null);
  useEffect(() => {
    const unsub = useAutomationStore.subscribe((state) => {
      if (state.lastClickedParamID !== lastClickedRef.current) {
        lastClickedRef.current = state.lastClickedParamID;
        if (!mountedRef.current) return;
        if (state.pinnedLaneParamID != null) return;
        if (state.lastClickedParamID == null) return;
        if (state.activeTrackIndex == null) return;
        const paramID = state.lastClickedParamID;
        const existingLane = state.lanes.find((l) => l.paramID === paramID);
        if (existingLane) {
          const reordered = [existingLane, ...state.lanes.filter((l) => l.laneIndex !== existingLane.laneIndex)];
          useAutomationStore.setState({ lanes: reordered });
        } else {
          const laneName = PARAM_NAMES[paramID] ?? `Param ${paramID}`;
          rpc.call("project.addAutomationLane", {
            trackIndex: state.activeTrackIndex,
            laneName,
            paramID,
          }).then(() => {
            useAutomationStore.getState().fetchForTrack(state.activeTrackIndex!, rpc);
          }).catch(() => {});
        }
      }
    });
    mountedRef.current = true;
    return unsub;
  }, [rpc]);

  const handleAddLane = async () => {
    if (activeTrackIndex === null) return;
    const select = addLaneSelectRef.current;
    if (!select || !select.value) return;
    const [slotStr, idxStr] = select.value.split(":");
    const slotIndex = Number(slotStr);
    const paramIndex = Number(idxStr);
    const chosen = automatableParams.find((p) => p.slotIndex === slotIndex && p.paramIndex === paramIndex);
    if (!chosen) return;
    // Compound paramID consumed by the engine (see composePid): audio entries
    // compose 100 + slotIndex*100 + paramIndex; MIDI-FX entries (paramIndex
    // >= 1000) pass the full pid through. Lane name is prefixed with the slot
    // so two plugins that each expose a param named "Gain" don't collide on
    // the name-keyed store/RPC path.
    const paramID = composePid(slotIndex, paramIndex);
    const laneName = `S${slotIndex} ${chosen.name}`;
    try {
      await rpc.call("project.addAutomationLane", {
        trackIndex: activeTrackIndex,
        laneName,
        paramID,
      });
      await fetchForTrack(activeTrackIndex, rpc);
    } catch (err) {
      console.error("Add lane failed:", err);
    }
  };

  const handleRemoveLane = async (laneName: string) => {
    if (activeTrackIndex === null) return;
    try {
      await removeLane(activeTrackIndex, laneName, rpc);
    } catch (err) {
      console.error("Remove lane failed:", err);
    }
  };

  const handlePinToggle = () => {
    if (pinnedLaneParamID != null) {
      setPinnedLaneParamID(null);
    } else {
      const primaryLane = lanes[0];
      if (primaryLane) {
        setPinnedLaneParamID(primaryLane.paramID);
      }
    }
  };

  const isPinned = pinnedLaneParamID != null;
  const primaryLane = lanes[0];

  // Params not yet bound to a lane — keyed by the compound paramID so the same
  // plugin param can't be added twice. (Previously this compared a set of
  // paramIDs against a paramIndex — a namespace mismatch that always passed.)
  const boundParamIDs = new Set(lanes.map((l) => l.paramID));
  const availableParams = automatableParams.filter(
    (p) => !boundParamIDs.has(composePid(p.slotIndex, p.paramIndex))
  );

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    const { lanes, selectedPointTimes, activeTrackIndex, removePoints, selectAll, clearSelection } = useAutomationStore.getState();
    if (activeTrackIndex === null) return;

    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      for (const lane of lanes) {
        const sel = selectedPointTimes.get(lane.name);
        if (sel && sel.size > 0) {
          removePoints(activeTrackIndex, lane.name, [...sel], rpc);
          clearSelection(lane.name);
          break;
        }
      }
      return;
    }

    for (const lane of lanes) {
      const sel = selectedPointTimes.get(lane.name);
      if (sel && sel.size > 0) {
        if (e.code === "KeyA" && (e.ctrlKey || e.metaKey)) {
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
        <select
          ref={addLaneSelectRef}
          className="ap-param-select"
          disabled={availableParams.length === 0}
          value=""
        >
          {availableParams.length === 0 ? (
            <option value="">{automatableParams.length === 0 ? "No automatable params" : "All params added"}</option>
          ) : (
            availableParams.map((p) => (
              <option key={`${p.slotIndex}:${p.paramIndex}`} value={`${p.slotIndex}:${p.paramIndex}`}>
                S{p.slotIndex} · {p.name}
              </option>
            ))
          )}
        </select>
        <button
          className="ap-add-btn"
          onClick={handleAddLane}
          disabled={availableParams.length === 0}
        >
          + Add Lane
        </button>
        {primaryLane && (
          <button
            className={`ap-pin-btn${isPinned ? " ap-pin-btn--pinned" : ""}`}
            onClick={handlePinToggle}
            title={isPinned ? `Pinned to ${primaryLane.name} (click to unpin)` : "Pin primary lane"}
          >
            Pin
          </button>
        )}
        <span className="ap-track-label">Track {activeTrackIndex}</span>
      </div>
      <div className="ap-canvas-list">
        {lanes.length === 0 && (
          <div className="ap-empty-no-lanes">No automation lanes. Pick a parameter above and click + Add Lane.</div>
        )}
        {lanes.map((lane) => (
          <div key={lane.laneIndex} className="ap-lane-row">
            <div className="ap-mode-btns">
              <button
                className={`ap-enable-btn${lane.enabled !== false ? " ap-enable-btn--on" : ""}`}
                onClick={() => {
                  rpc.call("project.setAutomationEnabled", {
                    trackIndex: activeTrackIndex,
                    lane: lane.name,
                    enabled: lane.enabled === false,
                  });
                  fetchForTrack(activeTrackIndex!, rpc);
                }}
                title={lane.enabled !== false ? "Disable lane" : "Enable lane"}
              >
                {lane.enabled !== false ? "ON" : "OFF"}
              </button>
              {(["read", "write", "touch", "latch"] as const).map((m) => (
                <button
                  key={m}
                  className={`ap-mode-btn${(lane.mode ?? "read") === m ? " ap-mode-btn--active" : ""}`}
                  onClick={() => {
                    rpc.call("project.setAutomationMode", { trackIndex: activeTrackIndex, laneName: lane.name, mode: m });
                    fetchForTrack(activeTrackIndex!, rpc);
                  }}
                >
                  {m[0].toUpperCase()}
                </button>
              ))}
            </div>
            <AutomationLaneCanvas
              laneName={lane.name}
              points={pointsByLane.get(lane.name) ?? []}
              trackIndex={activeTrackIndex!}
              rpc={rpc}
              viewStartBeat={0}
              viewEndBeat={32}
              onRemoveLane={() => handleRemoveLane(lane.name)}
            />
            <EnvelopeGenerateControl
              collapsed
              onGenerate={(params) => {
                useAutomationStore.getState().generateEnvelope(activeTrackIndex!, lane.name, params, rpc);
              }}
            />
          </div>
        ))}
      </div>
    </div>
  );
}
