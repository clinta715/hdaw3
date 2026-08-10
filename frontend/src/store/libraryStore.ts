import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { reportRpcError } from "./notifyStore";

export interface LibraryInfo {
  id: string;
  name: string;
  path: string;
  type: "midi" | "audio";
  lastScan: string;
  fileCount: number;
  autoScan: boolean;
}

export interface LibraryEntry {
  name: string;
  path: string;
  size: number;
  durationSeconds: number;
  key: string;
  tracks?: number;
  notes?: number;
  sampleRate?: number;
  channels?: number;
  bpm?: number;
  format?: string;
  timeSignature?: string;
}

export interface ScanProgress {
  libraryId: string;
  scanned: number;
  total: number;
  phase: string;
}

export interface LibrarySearchFilters {
  type?: string;
  libraryId?: string;
  durationMin?: number;
  durationMax?: number;
  bpmMin?: number;
  bpmMax?: number;
  key?: string;
  offset?: number;
  limit?: number;
}

interface LibraryState {
  libraries: LibraryInfo[];
  searchResults: LibraryEntry[];
  searchQuery: string;
  scanProgress: Record<string, ScanProgress>;
  loading: boolean;

  // All actions take rpc via DI (matches arrangerStore / automationStore).
  loadLibraries: (rpc: RpcClient) => Promise<void>;
  addLibrary: (name: string, path: string, type: "midi" | "audio", rpc: RpcClient) => Promise<boolean>;
  removeLibrary: (id: string, rpc: RpcClient) => Promise<void>;
  scanLibrary: (id: string, rpc: RpcClient) => Promise<void>;
  scanAll: (rpc: RpcClient) => Promise<void>;
  setSearchQuery: (q: string) => void;
  search: (query: string, filters: LibrarySearchFilters, rpc: RpcClient) => Promise<void>;
  setAutoScan: (id: string, enabled: boolean, rpc: RpcClient) => Promise<void>;
  updateScanProgress: (progress: ScanProgress) => void;
  clearScanProgress: (libraryId: string) => void;
}

export const useLibraryStore = create<LibraryState>((set, get) => ({
  libraries: [],
  searchResults: [],
  searchQuery: "",
  scanProgress: {},
  loading: false,

  loadLibraries: async (rpc) => {
    set({ loading: true });
    try {
      const result = await rpc.call("library.list");
      set({ libraries: Array.isArray(result) ? (result as LibraryInfo[]) : [], loading: false });
    } catch (err) {
      set({ loading: false });
      reportRpcError("library.list", err);
    }
  },

  addLibrary: async (name, path, type, rpc) => {
    try {
      await rpc.call("library.add", { name, path, type });
      await get().loadLibraries(rpc);
      return true;
    } catch (err) {
      reportRpcError("library.add", err);
      return false;
    }
  },

  removeLibrary: async (id, rpc) => {
    try {
      await rpc.call("library.remove", { id });
      await get().loadLibraries(rpc);
    } catch (err) {
      reportRpcError("library.remove", err);
    }
  },

  scanLibrary: async (id, rpc) => {
    try {
      await rpc.call("library.scan", { id });
    } catch (err) {
      reportRpcError("library.scan", err);
    }
  },

  scanAll: async (rpc) => {
    try {
      await rpc.call("library.scan", {});
    } catch (err) {
      reportRpcError("library.scan", err);
    }
  },

  setSearchQuery: (q) => set({ searchQuery: q }),

  search: async (query, filters, rpc) => {
    try {
      const result = await rpc.call("library.search", { query, ...filters });
      set({ searchResults: Array.isArray(result) ? (result as LibraryEntry[]) : [] });
    } catch (err) {
      reportRpcError("library.search", err);
    }
  },

  setAutoScan: async (id, enabled, rpc) => {
    try {
      await rpc.call("library.setAutoScan", { id, enabled });
      await get().loadLibraries(rpc);
    } catch (err) {
      reportRpcError("library.setAutoScan", err);
    }
  },

  updateScanProgress: (progress) =>
    set((s) => ({
      scanProgress: { ...s.scanProgress, [progress.libraryId]: progress },
    })),

  clearScanProgress: (libraryId) =>
    set((s) => {
      const next = { ...s.scanProgress };
      delete next[libraryId];
      return { scanProgress: next };
    }),
}));
