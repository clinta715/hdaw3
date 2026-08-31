import { create } from "zustand";
import { ClipSnapshot } from "../rpc/types";

export const MIN_BOTTOM_PANEL_H = 120;
const BOTTOM_PANEL_H_KEY = "hdaw_bottom_panel_h";
const BOTTOM_PANEL_H_PER_TAB_KEY = "hdaw_bottom_panel_h_per_tab";
const HDAW_VIEWMODE_KEY = "hdaw_view_mode";
const HDAW_LAST_TAB_KEY = "hdaw_last_bottom_tab";
const SNAP_ENABLED_KEY = "hdaw_snapEnabled";
const SNAP_DIVISION_KEY = "hdaw_snapDivision";
const SNAP_GRID_OFFSET_KEY = "hdaw_snapGridOffset";
const SNAP_TO_EVENTS_KEY = "hdaw_snapToEvents";

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
  "fm-analysis",
  "psy-fm",
  "tempo",
  "presets",
] as const;
export type BottomTabId = (typeof BOTTOM_TAB_IDS)[number];
export const DEFAULT_BOTTOM_TAB = "mixer";
export const DEFAULT_VIEW_MODE = "arrange" as const;

export const TAB_DEFAULT_HEIGHTS: Partial<Record<BottomTabId, number>> = { "piano-roll": 300 };

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

function loadBottomPanelHeights(): Record<string, number> {
  try {
    const raw = localStorage.getItem(BOTTOM_PANEL_H_PER_TAB_KEY);
    if (raw != null) {
      const parsed: unknown = JSON.parse(raw);
      if (parsed != null && typeof parsed === "object" && !Array.isArray(parsed)) {
        const out: Record<string, number> = {};
        for (const [tab, h] of Object.entries(parsed as Record<string, unknown>)) {
          if (
            (BOTTOM_TAB_IDS as readonly string[]).includes(tab) &&
            typeof h === "number" &&
            Number.isFinite(h) &&
            Number.isInteger(h) &&
            h >= MIN_BOTTOM_PANEL_H
          ) {
            out[tab] = h;
          }
        }
        return out;
      }
    }
  } catch {
    /* storage unavailable or corrupt — fall through to default */
  }
  return {};
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

function loadSnapEnabled(): boolean {
  try {
    return localStorage.getItem(SNAP_ENABLED_KEY) !== "false";
  } catch {
    return true;
  }
}

function loadSnapDivision(): number {
  try {
    const v = localStorage.getItem(SNAP_DIVISION_KEY);
    return v != null ? Number(v) || 1 : 1;
  } catch {
    return 1;
  }
}

function loadSnapGridOffset(): boolean {
  try {
    return localStorage.getItem(SNAP_GRID_OFFSET_KEY) === "true";
  } catch {
    return false;
  }
}

function loadSnapToEvents(): boolean {
  try {
    return localStorage.getItem(SNAP_TO_EVENTS_KEY) === "true";
  } catch {
    return false;
  }
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
  bottomPanelHeights: Record<string, number>;
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
  setBottomPanelHeightForTab: (tab: string, h: number) => void;
  effectiveBottomPanelHeight: (tab: string) => number;
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
  snapEnabled: loadSnapEnabled(),
  snapDivision: loadSnapDivision(),
  snapGridOffset: loadSnapGridOffset(),
  snapToEvents: loadSnapToEvents(),
  showPhraseGenerator: false,
  bottomPanelHeight: loadBottomPanelHeight(),
  bottomPanelHeights: loadBottomPanelHeights(),
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

  setSnapEnabled: (enabled) => {
    try { localStorage.setItem(SNAP_ENABLED_KEY, String(enabled)); } catch {}
    set({ snapEnabled: enabled });
  },
  setSnapDivision: (division) => {
    try { localStorage.setItem(SNAP_DIVISION_KEY, String(division)); } catch {}
    set({ snapDivision: division });
  },
  setSnapGridOffset: (enabled) => {
    try { localStorage.setItem(SNAP_GRID_OFFSET_KEY, String(enabled)); } catch {}
    set({ snapGridOffset: enabled });
  },
  setSnapToEvents: (enabled) => {
    try { localStorage.setItem(SNAP_TO_EVENTS_KEY, String(enabled)); } catch {}
    set({ snapToEvents: enabled });
  },
  setShowPhraseGenerator: (show) => set({ showPhraseGenerator: show }),
  setBottomPanelHeight: (h) => {
    try {
      localStorage.setItem(BOTTOM_PANEL_H_KEY, String(h));
    } catch {
      /* storage unavailable — height still applies for this session */
    }
    set({ bottomPanelHeight: h });
  },
  setBottomPanelHeightForTab: (tab, h) => {
    if (!(BOTTOM_TAB_IDS as readonly string[]).includes(tab)) return;
    const next = { ...get().bottomPanelHeights, [tab]: Math.max(MIN_BOTTOM_PANEL_H, h) };
    try {
      localStorage.setItem(BOTTOM_PANEL_H_PER_TAB_KEY, JSON.stringify(next));
    } catch {
      /* storage unavailable — height still applies for this session */
    }
    set({ bottomPanelHeights: next });
  },
  effectiveBottomPanelHeight: (tab) => {
    const state = get();
    return (
      state.bottomPanelHeights[tab] ??
      TAB_DEFAULT_HEIGHTS[tab as BottomTabId] ??
      state.bottomPanelHeight
    );
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
