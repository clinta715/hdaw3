import { create } from "zustand";

interface BrowserState {
  folders: string[];
  expandedPaths: Set<string>;
  selectedFile: string | null;
  searchQuery: string;
  visible: boolean;
  addFolder: (path: string) => void;
  removeFolder: (path: string) => void;
  toggleExpanded: (path: string) => void;
  setSelectedFile: (path: string | null) => void;
  setSearchQuery: (q: string) => void;
  toggleVisible: () => void;
}

const STORAGE_KEY = "hdaw_browser_folders";

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

export const useBrowserStore = create<BrowserState>((set, get) => ({
  folders: loadFolders(),
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
