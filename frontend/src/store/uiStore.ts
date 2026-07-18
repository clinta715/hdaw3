import { create } from "zustand";

interface UiState {
  selectedClipId: number | null;
  selectClip: (id: number | null) => void;
}

export const useUiStore = create<UiState>((set) => ({
  selectedClipId: null,
  selectClip: (id) => set({ selectedClipId: id }),
}));
