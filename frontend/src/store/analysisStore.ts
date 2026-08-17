import { create } from "zustand";
import { FmAnalysisPayload, FmAnalysisSnapshot } from "../rpc/types";

interface AnalysisState {
  tracks: FmAnalysisSnapshot[];
  update: (data: FmAnalysisPayload) => void;
}

export const useAnalysisStore = create<AnalysisState>((set) => ({
  tracks: [],
  update: (data) => set({ tracks: data.tracks }),
}));
