import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { ProjectSnapshot, TrackSnapshot, ClipSnapshot, NoteSnapshot } from "../rpc/types";

interface ProjectState {
  snapshot: ProjectSnapshot | null;
  notesByClip: Map<number, NoteSnapshot[]>;
  lastSync: number;
  isDirty: boolean;
  filePath: string | null;
  recentProjects: string[];

  syncSnapshot: (rpc: RpcClient) => Promise<void>;
  syncDirtyFlag: (rpc: RpcClient) => Promise<void>;
  syncNotes: (rpc: RpcClient, clipId: number) => Promise<void>;
  getTrack: (index: number) => TrackSnapshot | undefined;
  getClip: (clipId: number) => ClipSnapshot | undefined;
  setFilePath: (path: string | null) => void;
  loadRecentProjects: () => void;
  addRecentProject: (path: string) => void;
}

export const useProjectStore = create<ProjectState>((set, get) => ({
  snapshot: null,
  notesByClip: new Map(),
  lastSync: 0,
  isDirty: false,
  filePath: null,
  recentProjects: JSON.parse(localStorage.getItem("hdaw_recent_projects") || "[]"),

  syncSnapshot: async (rpc: RpcClient) => {
    const result = await rpc.call("read.snapshot") as ProjectSnapshot;
    set({ snapshot: result, lastSync: Date.now() });
    // Sync markers after project snapshot loads
    const { useMarkerStore } = await import("./markerStore");
    useMarkerStore.getState().syncMarkers(rpc);
  },

  syncDirtyFlag: async (rpc: RpcClient) => {
    const dirty = await rpc.call("read.isDirty").catch(() => false);
    set({ isDirty: !!dirty });
  },

  syncNotes: async (rpc: RpcClient, clipId: number) => {
    const result = await rpc.call("read.getNotes", { clipId }) as NoteSnapshot[];
    const notesByClip = new Map(get().notesByClip);
    notesByClip.set(clipId, result);
    set({ notesByClip });
  },

  getTrack: (index: number) => {
    return get().snapshot?.tracks.find((t) => t.index === index);
  },

  getClip: (clipId: number) => {
    return get().snapshot?.clips.find((c) => c.clipId === clipId);
  },

  setFilePath: (path) => set({ filePath: path }),

  loadRecentProjects: () => set({ recentProjects: JSON.parse(localStorage.getItem("hdaw_recent_projects") || "[]") }),

  addRecentProject: (path) => set((state) => {
    const list = [path, ...state.recentProjects.filter(p => p !== path)].slice(0, 8);
    localStorage.setItem("hdaw_recent_projects", JSON.stringify(list));
    return { recentProjects: list, filePath: path };
  }),
}));
