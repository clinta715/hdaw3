import { create } from "zustand";

// Handoff store for the "Render with RAVE…" clip-context-menu action. The
// TimelineContextMenu writes the pending clip here and switches the bottom
// panel to the Neural tab; NeuralPanel reads it to prefill input/output paths
// and import defaults. Store-based handoff (not component props) keeps the
// context menu and the panel decoupled — they live in different subtrees.

export interface PendingRaveClip {
  clipId: number;
  name: string;
  sourceFile: string;
  trackIndex: number;
  startBeat: number;
}

interface NeuralState {
  pendingClip: PendingRaveClip | null;
  setPendingClip: (clip: PendingRaveClip) => void;
  clearPendingClip: () => void;
}

export const useNeuralStore = create<NeuralState>((set) => ({
  pendingClip: null,
  setPendingClip: (clip) => set({ pendingClip: clip }),
  clearPendingClip: () => set({ pendingClip: null }),
}));
