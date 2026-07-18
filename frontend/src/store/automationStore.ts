import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { AutomationLaneSnapshot, AutomationPointSnapshot } from "../rpc/types";

interface AutomationState {
  lanes: AutomationLaneSnapshot[];
  pointsByLane: Map<string, AutomationPointSnapshot[]>;
  activeTrackIndex: number | null;
  loading: boolean;
  error: string | null;

  fetchForTrack: (trackIndex: number, rpc: RpcClient) => Promise<void>;
  clear: () => void;
}

export const useAutomationStore = create<AutomationState>((set, get) => ({
  lanes: [],
  pointsByLane: new Map(),
  activeTrackIndex: null,
  loading: false,
  error: null,

  fetchForTrack: async (trackIndex: number, rpc: RpcClient) => {
    set({ loading: true, error: null, activeTrackIndex: trackIndex });
    try {
      const lanes = await rpc.call("read.getAutomationLanes", { trackIndex }) as AutomationLaneSnapshot[];
      const pointsByLane = new Map<string, AutomationPointSnapshot[]>();
      for (const lane of lanes) {
        const pts = await rpc.call("read.getAutomationPoints", { trackIndex, laneName: lane.name }) as AutomationPointSnapshot[];
        pointsByLane.set(lane.name, pts);
      }
      set({ lanes, pointsByLane, loading: false });
    } catch (err) {
      set({ loading: false, error: String(err) });
    }
  },

  clear: () => {
    set({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, error: null });
  },
}));
