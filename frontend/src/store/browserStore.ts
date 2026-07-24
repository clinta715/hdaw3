import { create } from "zustand";

interface FavoriteFolder {
  path: string;
  label: string;
}

interface BrowserState {
  folders: string[];
  favorites: FavoriteFolder[];
  expandedPaths: Set<string>;
  selectedFile: string | null;
  searchQuery: string;
  visible: boolean;
  addFolder: (path: string) => void;
  removeFolder: (path: string) => void;
  addFavorite: (path: string, label?: string) => void;
  removeFavorite: (path: string) => void;
  moveFavorite: (fromIndex: number, toIndex: number) => void;
  toggleExpanded: (path: string) => void;
  setSelectedFile: (path: string | null) => void;
  setSearchQuery: (q: string) => void;
  toggleVisible: () => void;
}

const STORAGE_KEY = "hdaw_browser_folders";
const FAVORITES_KEY = "hdaw_browser_favorites";

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

export const useBrowserStore = create<BrowserState>((set, get) => ({
  folders: loadFolders(),
  favorites: loadFavorites(),
  expandedPaths: new Set<string>(),
  selectedFile: null,
  searchQuery: "",
  visible: false,

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
    set({ expandedPaths: next });
  },

  setSelectedFile: (path) => set({ selectedFile: path }),
  setSearchQuery: (q) => set({ searchQuery: q }),
  toggleVisible: () => set((s) => ({ visible: !s.visible })),
}));
