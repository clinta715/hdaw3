import { create } from "zustand";
import { ClipSnapshot } from "../rpc/types";

export const MIN_BOTTOM_PANEL_H = 120;
const BOTTOM_PANEL_H_KEY = "hdaw_bottom_panel_h";
const HDAW_VIEWMODE_KEY = "hdaw_view_mode";
const HDAW_LAST_TAB_KEY = "hdaw_last_bottom_tab";

export const BOTTOM_TAB_IDS = [
  "mixer",
  "piano-roll",
  "automation",
  "fx",
  "midi-fx",
  "audio-editor",
  "modulation",
  "step-seq",
  "undo-history",
  "inspector",
  "sampler",
  "arranger",
] as const;
export type BottomTabId = (typeof BOTTOM_TAB_IDS)[number];
export const DEFAULT_BOTTOM_TAB = "mixer";
export const DEFAULT_VIEW_MODE = "arrange" as const;

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

function loadViewMode(): "arrange" | "session" {
  try {
    const vm = localStorage.getItem(HDAW_VIEWMODE_KEY);
    if (vm === "arrange" || vm === "session") return vm;
  } catch {
    /* storage unavailable (e.g. test env) — fall through to default */
  }
  return DEFAULT_VIEW_MODE;
}

function loadLastBottomTab(): string {
  try {
    const lt = localStorage.getItem(HDAW_LAST_TAB_KEY);
    if (lt && (BOTTOM_TAB_IDS as readonly string[]).includes(lt)) return lt;
  } catch {
    /* storage unavailable (e.g. test env) — fall through to default */
  }
  return DEFAULT_BOTTOM_TAB;
}

interface UiState {
  selectedClipIds: Set<number>;
  lastSelectedClipId: number | null;
  selectedTrackIndex: number | null;
  clipClipboard: ClipSnapshot[];
  activeBottomTab: string;
  snapEnabled: boolean;
  snapDivision: number;
  snapGridOffset: boolean;
  snapToEvents: boolean;
  showPhraseGenerator: boolean;
  bottomPanelHeight: number;
  viewMode: "arrange" | "session";
  statusHint: string | null;
  crashedFxSlots: Record<string, { pluginName: string }>;

  selectClip: (id: number | null, trackIndex?: number | null) => void;
  toggleClipSelection: (id: number) => void;
  selectRange: (fromId: number, toId: number, clips: ClipSnapshot[]) => void;
  selectAllClips: (clips: ClipSnapshot[]) => void;
  clearSelection: () => void;
  setClipboard: (clips: ClipSnapshot[]) => void;
  setActiveBottomTab: (tab: string) => void;
  selectBottomTab: (tab: string) => void;
  setSnapEnabled: (enabled: boolean) => void;
  setSnapDivision: (division: number) => void;
  setSnapGridOffset: (enabled: boolean) => void;
  setSnapToEvents: (enabled: boolean) => void;
  setShowPhraseGenerator: (show: boolean) => void;
  setBottomPanelHeight: (h: number) => void;
  setViewMode: (mode: "arrange" | "session") => void;
  setStatusHint: (h: string | null) => void;
  setSlotCrashed: (trackIndex: number, pluginId: string, pluginName: string) => void;
  clearSlotCrashed: (trackIndex: number, pluginId: string) => void;
}

export const useUiStore = create<UiState>((set, get) => ({
  selectedClipIds: new Set(),
  lastSelectedClipId: null,
  selectedTrackIndex: null,
  clipClipboard: [],
  activeBottomTab: loadLastBottomTab(),
  snapEnabled: true,
  snapDivision: 1,
  snapGridOffset: false,
  snapToEvents: false,
  showPhraseGenerator: false,
  bottomPanelHeight: loadBottomPanelHeight(),
  viewMode: loadViewMode(),
  statusHint: null,
  crashedFxSlots: {},

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

  selectBottomTab: (tab) => {
    if (!(BOTTOM_TAB_IDS as readonly string[]).includes(tab)) return;
    try {
      localStorage.setItem(HDAW_LAST_TAB_KEY, tab);
    } catch {
      /* storage unavailable — tab still applies for this session */
    }
    set({ activeBottomTab: tab });
  },

  setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),
  setSnapDivision: (division) => set({ snapDivision: division }),
  setSnapGridOffset: (enabled) => set({ snapGridOffset: enabled }),
  setSnapToEvents: (enabled) => set({ snapToEvents: enabled }),
  setShowPhraseGenerator: (show) => set({ showPhraseGenerator: show }),
  setBottomPanelHeight: (h) => {
    try {
      localStorage.setItem(BOTTOM_PANEL_H_KEY, String(h));
    } catch {
      /* storage unavailable — height still applies for this session */
    }
    set({ bottomPanelHeight: h });
  },
  setViewMode: (mode) => {
    try {
      localStorage.setItem(HDAW_VIEWMODE_KEY, mode);
    } catch {
      /* storage unavailable — mode still applies for this session */
    }
    set({ viewMode: mode });
  },
  setStatusHint: (h) => set({ statusHint: h }),

  setSlotCrashed: (trackIndex, pluginId, pluginName) => set((state) => ({
    crashedFxSlots: {
      ...state.crashedFxSlots,
      [`${trackIndex}:${pluginId}`]: { pluginName },
    },
  })),

  clearSlotCrashed: (trackIndex, pluginId) => set((state) => {
    const key = `${trackIndex}:${pluginId}`;
    if (!(key in state.crashedFxSlots)) return state;
    const next = { ...state.crashedFxSlots };
    delete next[key];
    return { crashedFxSlots: next };
  }),
}));
