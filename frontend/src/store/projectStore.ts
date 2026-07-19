import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { ProjectSnapshot, TrackSnapshot, ClipSnapshot, NoteSnapshot } from "../rpc/types";

interface ProjectState {
  snapshot: ProjectSnapshot | null;
  notesByClip: Map<number, NoteSnapshot[]>;
  lastSync: number;
  isDirty: boolean;

  syncSnapshot: (rpc: RpcClient) => Promise<void>;
  syncDirtyFlag: (rpc: RpcClient) => Promise<void>;
  syncNotes: (rpc: RpcClient, clipId: number) => Promise<void>;
  getTrack: (index: number) => TrackSnapshot | undefined;
  getClip: (clipId: number) => ClipSnapshot | undefined;
}

export const useProjectStore = create<ProjectState>((set, get) => ({
  snapshot: null,
  notesByClip: new Map(),
  lastSync: 0,
  isDirty: false,

  syncSnapshot: async (rpc: RpcClient) => {
    const result = await rpc.call("read.snapshot") as ProjectSnapshot;
    set({ snapshot: result, lastSync: Date.now() });
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
}));
