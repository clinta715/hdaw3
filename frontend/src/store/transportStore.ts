import { create } from "zustand";
import { TransportSnapshot } from "../rpc/types";

interface TransportState {
  transport: TransportSnapshot;
  update: (data: TransportSnapshot) => void;
}

export const useTransportStore = create<TransportState>((set) => ({
  transport: {
    bpm: 120,
    isPlaying: false,
    isLooping: false,
    isRecording: false,
    loopStart: 0,
    loopEnd: 8,
    currentTimeSeconds: 0,
    sampleRate: 0,
  },
  update: (data) => set({ transport: data }),
}));
