import { create } from "zustand";

interface UiState {
  selectedClipId: number | null;
  selectedTrackIndex: number | null;
  snapEnabled: boolean;
  snapDivision: number; // 0=Bar, 1=Beat, 2=1/8, 3=1/16, 4=1/32
  selectClip: (id: number | null, trackIndex?: number | null) => void;
  setSnapEnabled: (enabled: boolean) => void;
  setSnapDivision: (division: number) => void;
}

export const useUiStore = create<UiState>((set) => ({
  selectedClipId: null,
  selectedTrackIndex: null,
  snapEnabled: true,
  snapDivision: 1, // Beat default
  selectClip: (id, trackIndex) => set({ selectedClipId: id, selectedTrackIndex: trackIndex ?? null }),
  setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),
  setSnapDivision: (division) => set({ snapDivision: division }),
}));
