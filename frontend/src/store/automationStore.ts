import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { AutomationLaneSnapshot, AutomationPointSnapshot } from "../rpc/types";

const HDAW_PINNED_LANE_KEY = "hdaw_pinned_lane_param_id";

function loadPinnedLaneParamID(): number | null {
  try {
    const raw = localStorage.getItem(HDAW_PINNED_LANE_KEY);
    if (raw != null) {
      const n = parseInt(raw, 10);
      if (Number.isFinite(n) && n >= 0) return n;
    }
  } catch { /* storage unavailable */ }
  return null;
}

interface AutomationState {
  lanes: AutomationLaneSnapshot[];
  pointsByLane: Map<string, AutomationPointSnapshot[]>;
  activeTrackIndex: number | null;
  loading: boolean;
  error: string | null;
  selectedPointTimes: Map<string, Set<number>>;
  pinnedLaneParamID: number | null;
  lastClickedParamID: number | null;

  fetchForTrack: (trackIndex: number, rpc: RpcClient) => Promise<void>;
  clear: () => void;

  selectPoint: (laneName: string, time: number, shift: boolean, ctrl: boolean) => void;
  selectAll: (laneName: string) => void;
  clearSelection: (laneName?: string) => void;
  addPoint: (trackIndex: number, laneName: string, time: number, value: number, rpc: RpcClient) => Promise<void>;
  removePoints: (trackIndex: number, laneName: string, times: number[], rpc: RpcClient) => Promise<void>;
  removeLane: (trackIndex: number, laneName: string, rpc: RpcClient) => Promise<void>;
  setPinnedLaneParamID: (paramID: number | null) => void;
  setLastClickedParamID: (paramID: number | null) => void;
}

export const useAutomationStore = create<AutomationState>((set, get) => ({
  lanes: [],
  pointsByLane: new Map(),
  activeTrackIndex: null,
  loading: false,
  error: null,
  selectedPointTimes: new Map(),
  pinnedLaneParamID: loadPinnedLaneParamID(),
  lastClickedParamID: null,

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
    set({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, error: null, selectedPointTimes: new Map() });
  },

  selectPoint: (laneName: string, time: number, shift: boolean, ctrl: boolean) => {
    const sel = new Map(get().selectedPointTimes);
    const laneSel = new Set(sel.get(laneName) ?? []);
    if (ctrl) {
      if (laneSel.has(time)) laneSel.delete(time);
      else laneSel.add(time);
    } else if (shift) {
      const pts = get().pointsByLane.get(laneName) ?? [];
      const sorted = pts.map((p) => p.time).sort((a, b) => a - b);
      const clickIdx = sorted.indexOf(time);
      if (laneSel.size > 0 && clickIdx >= 0) {
        const existingTimes = [...laneSel].map((t) => sorted.indexOf(t)).filter((i) => i >= 0);
        if (existingTimes.length > 0) {
          const lastIdx = existingTimes[existingTimes.length - 1];
          const minIdx = Math.min(lastIdx, clickIdx);
          const maxIdx = Math.max(lastIdx, clickIdx);
          for (let i = minIdx; i <= maxIdx; i++) laneSel.add(sorted[i]);
        } else {
          laneSel.add(time);
        }
      } else {
        laneSel.add(time);
      }
    } else {
      laneSel.clear();
      laneSel.add(time);
    }
    sel.set(laneName, laneSel);
    set({ selectedPointTimes: sel });
  },

  selectAll: (laneName: string) => {
    const pts = get().pointsByLane.get(laneName) ?? [];
    const sel = new Map(get().selectedPointTimes);
    sel.set(laneName, new Set(pts.map((p) => p.time)));
    set({ selectedPointTimes: sel });
  },

  clearSelection: (laneName?: string) => {
    const sel = new Map(get().selectedPointTimes);
    if (laneName) sel.delete(laneName);
    else sel.clear();
    set({ selectedPointTimes: sel });
  },

  addPoint: async (trackIndex: number, laneName: string, time: number, value: number, rpc: RpcClient) => {
    await rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time, value });
    await get().fetchForTrack(trackIndex, rpc);
  },

  removePoints: async (trackIndex: number, laneName: string, times: number[], rpc: RpcClient) => {
    if (times.length === 0) return;
    // Wrap the loop in a transaction so the whole batch is one undo step.
    // Without this, each removeAutomationPoint call opens its own transaction
    // and the user has to press Ctrl+Z once per deleted point.
    if (times.length > 1) {
      await rpc.call("project.beginTransaction", { name: "remove automation points" });
    }
    for (const t of times) {
      await rpc.call("project.removeAutomationPoint", { trackIndex, lane: laneName, time: t });
    }
    if (times.length > 1) {
      await rpc.call("project.endTransaction");
    }
    await get().fetchForTrack(trackIndex, rpc);
  },

  removeLane: async (trackIndex: number, laneName: string, rpc: RpcClient) => {
    await rpc.call("project.removeAutomationLane", { trackIndex, laneName });
    await get().fetchForTrack(trackIndex, rpc);
  },

  setPinnedLaneParamID: (paramID: number | null) => {
    try {
      if (paramID != null) {
        localStorage.setItem(HDAW_PINNED_LANE_KEY, String(paramID));
      } else {
        localStorage.removeItem(HDAW_PINNED_LANE_KEY);
      }
    } catch { /* storage unavailable */ }
    set({ pinnedLaneParamID: paramID });
  },

  setLastClickedParamID: (paramID: number | null) => {
    set({ lastClickedParamID: paramID });
  },
}));
