import { create } from "zustand";
import { ClipSnapshot } from "../rpc/types";

export const MIN_BOTTOM_PANEL_H = 120;
const BOTTOM_PANEL_H_KEY = "hdaw_bottom_panel_h";

function loadBottomPanelHeight(): number {
  try {
    const raw = localStorage.getItem(BOTTOM_PANEL_H_KEY);
    if (raw != null) {
      const n = parseInt(raw, 10);
      if (Number.isFinite(n) && n >= MIN_BOTTOM_PANEL_H) return n;
    }
  } catch {
    /* storage unavailable (e.g. test env) — fall through to default */
  }
  return 200;
}

interface UiState {
  selectedClipIds: Set<number>;
  lastSelectedClipId: number | null;
  selectedTrackIndex: number | null;
  clipClipboard: ClipSnapshot[];
  activeBottomTab: string;
  snapEnabled: boolean;
  snapDivision: number;
  showPhraseGenerator: boolean;
  bottomPanelHeight: number;
  viewMode: "arrange" | "session";

  selectClip: (id: number | null, trackIndex?: number | null) => void;
  toggleClipSelection: (id: number) => void;
  selectRange: (fromId: number, toId: number, clips: ClipSnapshot[]) => void;
  selectAllClips: (clips: ClipSnapshot[]) => void;
  clearSelection: () => void;
  setClipboard: (clips: ClipSnapshot[]) => void;
  setActiveBottomTab: (tab: string) => void;
  setSnapEnabled: (enabled: boolean) => void;
  setSnapDivision: (division: number) => void;
  setShowPhraseGenerator: (show: boolean) => void;
  setBottomPanelHeight: (h: number) => void;
  setViewMode: (mode: "arrange" | "session") => void;
}

export const useUiStore = create<UiState>((set, get) => ({
  selectedClipIds: new Set(),
  lastSelectedClipId: null,
  selectedTrackIndex: null,
  clipClipboard: [],
  activeBottomTab: "mixer",
  snapEnabled: true,
  snapDivision: 1,
  showPhraseGenerator: false,
  bottomPanelHeight: loadBottomPanelHeight(),
  viewMode: "arrange",

  selectClip: (id, trackIndex) => set({
    selectedClipIds: id != null ? new Set([id]) : new Set<number>(),
    lastSelectedClipId: id,
    selectedTrackIndex: trackIndex ?? null,
  }),

  toggleClipSelection: (id) => set((state) => {
    const next = new Set(state.selectedClipIds);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    return { selectedClipIds: next, lastSelectedClipId: id };
  }),

  selectRange: (fromId, toId, clips) => set((state) => {
    const fromClip = clips.find(c => c.clipId === fromId);
    const toClip = clips.find(c => c.clipId === toId);
    if (!fromClip || !toClip) return state;
    const trackIdx = fromClip.trackIndex;
    const minBeat = Math.min(fromClip.startBeat, toClip.startBeat);
    const maxBeat = Math.max(fromClip.startBeat, toClip.startBeat);
    const next = new Set(state.selectedClipIds);
    for (const c of clips) {
      if (c.trackIndex === trackIdx && c.startBeat >= minBeat && c.startBeat <= maxBeat) {
        next.add(c.clipId);
      }
    }
    return { selectedClipIds: next, lastSelectedClipId: toId };
  }),

  selectAllClips: (clips) => set({
    selectedClipIds: new Set(clips.map(c => c.clipId)),
  }),

  clearSelection: () => set({ selectedClipIds: new Set(), lastSelectedClipId: null }),

  setClipboard: (clips) => set({ clipClipboard: clips }),

  setActiveBottomTab: (tab) => set({ activeBottomTab: tab }),

  setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),
  setSnapDivision: (division) => set({ snapDivision: division }),
  setShowPhraseGenerator: (show) => set({ showPhraseGenerator: show }),
  setBottomPanelHeight: (h) => {
    try {
      localStorage.setItem(BOTTOM_PANEL_H_KEY, String(h));
    } catch {
      /* storage unavailable — height still applies for this session */
    }
    set({ bottomPanelHeight: h });
  },
  setViewMode: (mode) => set({ viewMode: mode }),
}));
