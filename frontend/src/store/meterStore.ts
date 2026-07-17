import { create } from "zustand";
import { MetersPayload, MeterLevels } from "../rpc/types";

interface MeterState {
  master: MeterLevels;
  tracks: MeterLevels[];
  update: (data: MetersPayload) => void;
}

export const useMeterStore = create<MeterState>((set) => ({
  master: { l: 0, r: 0 },
  tracks: [],
  update: (data) => set({ master: data.master, tracks: data.tracks }),
}));
