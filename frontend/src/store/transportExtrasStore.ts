import { create } from "zustand";

interface TransportExtras {
  metronomeEnabled: boolean;
  followPlayhead: boolean;
  set: (partial: Partial<TransportExtras>) => void;
}

export const useTransportExtrasStore = create<TransportExtras>((set) => ({
  metronomeEnabled: false,
  followPlayhead: false,
  set: (p) => set(p),
}));
