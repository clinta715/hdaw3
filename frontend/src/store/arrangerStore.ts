import { create } from "zustand";
import { RpcClient } from "../rpc/client";

export interface ArrangerRegionSnapshot {
  regionID: string;
  name: string;
  startTime: number;
  duration: number;
  color: number;
}

export interface ChainEntrySnapshot {
  regionID: string;
  repeatCount: number;
}

export interface ArrangerChainSnapshot {
  chainID: string;
  name: string;
  isActive: boolean;
  entries: ChainEntrySnapshot[];
}

interface ArrangerState {
  regions: ArrangerRegionSnapshot[];
  chains: ArrangerChainSnapshot[];
  syncArranger: (rpc: RpcClient) => Promise<void>;
}

export const useArrangerStore = create<ArrangerState>((set) => ({
  regions: [],
  chains: [],
  syncArranger: async (rpc: RpcClient) => {
    try {
      const [regionsResult, chainsResult] = await Promise.all([
        rpc.call("read.getArrangerRegions"),
        rpc.call("read.getArrangerChains"),
      ]);
      set({
        regions: Array.isArray(regionsResult) ? (regionsResult as ArrangerRegionSnapshot[]) : [],
        chains: Array.isArray(chainsResult) ? (chainsResult as ArrangerChainSnapshot[]) : [],
      });
    } catch {
      // ignore — arranger data may not exist yet
    }
  },
}));
