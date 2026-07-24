import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { ProjectSnapshot, TrackSnapshot, ClipSnapshot, NoteSnapshot, TreeDelta } from "../rpc/types";

interface ProjectState {
  snapshot: ProjectSnapshot | null;
  notesByClip: Map<number, NoteSnapshot[]>;
  lastSync: number;
  isDirty: boolean;
  filePath: string | null;
  recentProjects: string[];
  loadingProject: boolean;
  loadProgress: { message: string; percent: number } | null;

  syncSnapshot: (rpc: RpcClient) => Promise<void>;
  syncDirtyFlag: (rpc: RpcClient) => Promise<void>;
  syncNotes: (rpc: RpcClient, clipId: number) => Promise<void>;
  applyDelta: (delta: TreeDelta) => void;
  pendingTempIds: Set<number>;
  pendingResolution: Map<number, number>;
  addPendingClip: (placeholder: ClipSnapshot) => void;
  resolvePending: (tempId: number, realId: number) => void;
  removePending: (tempId: number) => void;
  getTrack: (index: number) => TrackSnapshot | undefined;
  getClip: (clipId: number) => ClipSnapshot | undefined;
  setFilePath: (path: string | null) => void;
  loadRecentProjects: () => void;
  addRecentProject: (path: string) => void;
  setLoadingProject: (loading: boolean) => void;
  updateLoadProgress: (message: string, percent: number) => void;
}

export const useProjectStore = create<ProjectState>((set, get) => ({
  snapshot: null,
  notesByClip: new Map(),
  lastSync: 0,
  isDirty: false,
  filePath: null,
  recentProjects: JSON.parse(localStorage.getItem("hdaw_recent_projects") || "[]"),
  loadingProject: false,
  loadProgress: null,
  pendingTempIds: new Set(),
  pendingResolution: new Map(),

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

  applyDelta: (delta: TreeDelta) => {
    const snap = get().snapshot;
    if (!snap) return;

    let clips = snap.clips;
    if (delta.clipsRemoved && delta.clipsRemoved.length > 0) {
      const rm = new Set(delta.clipsRemoved);
      clips = clips.filter((c) => !rm.has(c.clipId));
    }
    if (delta.clipsUpserted && delta.clipsUpserted.length > 0) {
      const byId = new Map(clips.map((c) => [c.clipId, c] as const));
      for (const c of delta.clipsUpserted) byId.set(c.clipId, c);
      clips = [...byId.values()];
    }

    let tracks = snap.tracks;
    if (delta.tracksUpserted && delta.tracksUpserted.length > 0) {
      const byIdx = new Map(tracks.map((t) => [t.index, t] as const));
      for (const t of delta.tracksUpserted) byIdx.set(t.index, t);
      tracks = [...byIdx.values()].sort((a, b) => a.index - b.index);
    }

    // Swap resolved placeholders out for the real clips that just arrived.
    let pendingTempIds = get().pendingTempIds;
    let pendingResolution = get().pendingResolution;
    if (pendingResolution.size > 0 && delta.clipsUpserted) {
      const arrivedReal = new Set(delta.clipsUpserted.map((c) => c.clipId));
      const resolvedTemps: number[] = [];
      for (const [tempId, realId] of pendingResolution) {
        if (arrivedReal.has(realId)) resolvedTemps.push(tempId);
      }
      if (resolvedTemps.length > 0) {
        const resolvedSet = new Set(resolvedTemps);
        clips = clips.filter((c) => !resolvedSet.has(c.clipId));
        pendingTempIds = new Set([...pendingTempIds].filter((id) => !resolvedSet.has(id)));
        pendingResolution = new Map([...pendingResolution].filter(([t]) => !resolvedSet.has(t)));
      }
    }

    set({ snapshot: { ...snap, clips, tracks }, lastSync: Date.now(), pendingTempIds, pendingResolution });
  },

  addPendingClip: (placeholder: ClipSnapshot) => set((state) => {
    const snap = state.snapshot;
    if (!snap) return {};
    const pendingTempIds = new Set(state.pendingTempIds);
    pendingTempIds.add(placeholder.clipId);
    return {
      snapshot: { ...snap, clips: [...snap.clips, placeholder] },
      pendingTempIds,
    };
  }),

  resolvePending: (tempId: number, realId: number) => set((state) => {
    const pendingResolution = new Map(state.pendingResolution);
    pendingResolution.set(tempId, realId);
    return { pendingResolution };
  }),

  removePending: (tempId: number) => set((state) => {
    const snap = state.snapshot;
    const pendingTempIds = new Set(state.pendingTempIds);
    pendingTempIds.delete(tempId);
    const pendingResolution = new Map(state.pendingResolution);
    pendingResolution.delete(tempId);
    if (!snap) return { pendingTempIds, pendingResolution };
    return {
      snapshot: { ...snap, clips: snap.clips.filter((c) => c.clipId !== tempId) },
      pendingTempIds,
      pendingResolution,
    };
  }),

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

  setLoadingProject: (loading: boolean) => set({ loadingProject: loading, loadProgress: loading ? { message: "Loading...", percent: 0 } : null }),

  updateLoadProgress: (message: string, percent: number) => set({ loadProgress: { message, percent } }),
}));
