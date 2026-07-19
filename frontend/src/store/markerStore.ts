import { create } from "zustand";
import { RpcClient } from "../rpc/client";

export interface MarkerSnapshot {
  index: number;
  name: string;
  time: number;  // beat position
  color: number;
}

interface MarkerState {
  markers: MarkerSnapshot[];
  syncMarkers: (rpc: RpcClient) => Promise<void>;
}

export const useMarkerStore = create<MarkerState>((set) => ({
  markers: [],
  syncMarkers: async (rpc: RpcClient) => {
    try {
      const result = await rpc.call("read.getMarkers");
      if (Array.isArray(result)) {
        set({ markers: result as MarkerSnapshot[] });
      }
    } catch {
      // ignore
    }
  },
}));
