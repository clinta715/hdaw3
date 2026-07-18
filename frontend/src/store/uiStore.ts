import { create } from "zustand";

interface UiState {
  selectedClipId: number | null;
  selectedTrackIndex: number | null;
  selectClip: (id: number | null, trackIndex?: number | null) => void;
}

export const useUiStore = create<UiState>((set) => ({
  selectedClipId: null,
  selectedTrackIndex: null,
  selectClip: (id, trackIndex) => set({ selectedClipId: id, selectedTrackIndex: trackIndex ?? null }),
}));
