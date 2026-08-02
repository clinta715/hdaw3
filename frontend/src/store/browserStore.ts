import { create } from "zustand";

interface FavoriteFolder {
  path: string;
  label: string;
}

export type FileKindFilter = "all" | "devices" | "presets" | "samples" | "clips" | "midi";

interface BrowserState {
  folders: string[];
  favorites: FavoriteFolder[];
  expandedPaths: Set<string>;
  selectedFile: string | null;
  searchQuery: string;
  visible: boolean;
  autoPreview: boolean;
  tempoMatch: boolean;
  sourceBpm: number;
  kindFilter: FileKindFilter;
  addFolder: (path: string) => void;
  removeFolder: (path: string) => void;
  addFavorite: (path: string, label?: string) => void;
  removeFavorite: (path: string) => void;
  moveFavorite: (fromIndex: number, toIndex: number) => void;
  toggleExpanded: (path: string) => void;
  setSelectedFile: (path: string | null) => void;
  setSearchQuery: (q: string) => void;
  toggleVisible: () => void;
  setAutoPreview: (v: boolean) => void;
  setTempoMatch: (v: boolean) => void;
  setSourceBpm: (v: number) => void;
  setKindFilter: (f: FileKindFilter) => void;
}

const STORAGE_KEY = "hdaw_browser_folders";
const FAVORITES_KEY = "hdaw_browser_favorites";
const EXPANDED_KEY = "hdaw_browser_expanded";

function loadFolders(): string[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function saveFolders(folders: string[]) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(folders));
}

function loadFavorites(): FavoriteFolder[] {
  try {
    const raw = localStorage.getItem(FAVORITES_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function saveFavorites(favorites: FavoriteFolder[]) {
  localStorage.setItem(FAVORITES_KEY, JSON.stringify(favorites));
}

function loadExpanded(): string[] {
  try {
    const raw = localStorage.getItem(EXPANDED_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function saveExpanded(paths: string[]) {
  localStorage.setItem(EXPANDED_KEY, JSON.stringify(paths));
}

function loadAutoPreview(): boolean {
  try {
    return localStorage.getItem("hdaw_browser_autopreview") === "true";
  } catch {
    return false;
  }
}

function saveAutoPreview(v: boolean) {
  localStorage.setItem("hdaw_browser_autopreview", String(v));
}

function loadTempoMatch(): boolean {
  try {
    // Migrate from old syncPreview key if present
    const sync = localStorage.getItem("hdaw_browser_syncpreview");
    if (sync !== null) {
      localStorage.removeItem("hdaw_browser_syncpreview");
      const v = sync !== "false";
      localStorage.setItem("hdaw_browser_tempomatch", String(v));
      return v;
    }
    return localStorage.getItem("hdaw_browser_tempomatch") !== "false";
  } catch {
    return true;
  }
}

function saveTempoMatch(v: boolean) {
  localStorage.setItem("hdaw_browser_tempomatch", String(v));
}

function loadSourceBpm(): number {
  try {
    const raw = localStorage.getItem("hdaw_browser_sourcebpm");
    return raw ? parseFloat(raw) || 120 : 120;
  } catch {
    return 120;
  }
}

function saveSourceBpm(v: number) {
  localStorage.setItem("hdaw_browser_sourcebpm", String(v));
}

const KIND_FILTER_KEY = "hdaw_browser_kindfilter";

function loadKindFilter(): FileKindFilter {
  try {
    const raw = localStorage.getItem(KIND_FILTER_KEY);
    if (raw === "all" || raw === "devices" || raw === "presets" || raw === "samples" || raw === "clips" || raw === "midi") return raw;
    return "all";
  } catch {
    return "all";
  }
}

function saveKindFilter(v: FileKindFilter) {
  localStorage.setItem(KIND_FILTER_KEY, v);
}

export const useBrowserStore = create<BrowserState>((set, get) => ({
  folders: loadFolders(),
  favorites: loadFavorites(),
  expandedPaths: new Set<string>(loadExpanded()),
  selectedFile: null,
  searchQuery: "",
  visible: false,
  autoPreview: loadAutoPreview(),
  tempoMatch: loadTempoMatch(),
  sourceBpm: loadSourceBpm(),
  kindFilter: loadKindFilter(),

  addFolder: (path) => {
    const { folders } = get();
    if (folders.includes(path)) return;
    const next = [...folders, path];
    saveFolders(next);
    set({ folders: next });
  },

  removeFolder: (path) => {
    const { folders, expandedPaths } = get();
    const next = folders.filter((f) => f !== path);
    saveFolders(next);
    const nextExpanded = new Set(expandedPaths);
    nextExpanded.delete(path);
    saveExpanded([...nextExpanded]);
    set({ folders: next, expandedPaths: nextExpanded });
  },

  addFavorite: (path, label) => {
    const { favorites } = get();
    if (favorites.some((f) => f.path === path)) return;
    const name = label ?? path.split(/[\\/]/).pop() ?? path;
    const next = [...favorites, { path, label: name }];
    saveFavorites(next);
    set({ favorites: next });
  },

  removeFavorite: (path) => {
    const { favorites } = get();
    const next = favorites.filter((f) => f.path !== path);
    saveFavorites(next);
    set({ favorites: next });
  },

  moveFavorite: (fromIndex, toIndex) => {
    const { favorites } = get();
    if (fromIndex < 0 || fromIndex >= favorites.length) return;
    if (toIndex < 0 || toIndex >= favorites.length) return;
    const next = [...favorites];
    const [moved] = next.splice(fromIndex, 1);
    next.splice(toIndex, 0, moved);
    saveFavorites(next);
    set({ favorites: next });
  },

  toggleExpanded: (path) => {
    const { expandedPaths } = get();
    const next = new Set(expandedPaths);
    if (next.has(path)) next.delete(path);
    else next.add(path);
    saveExpanded([...next]);
    set({ expandedPaths: next });
  },

  setSelectedFile: (path) => set({ selectedFile: path }),
  setSearchQuery: (q) => set({ searchQuery: q }),
  toggleVisible: () => set((s) => ({ visible: !s.visible })),
  setAutoPreview: (v) => { saveAutoPreview(v); set({ autoPreview: v }); },
  setTempoMatch: (v) => { saveTempoMatch(v); set({ tempoMatch: v }); },
  setSourceBpm: (v) => { saveSourceBpm(v); set({ sourceBpm: v }); },
  setKindFilter: (f) => { saveKindFilter(f); set({ kindFilter: f }); },
}));
