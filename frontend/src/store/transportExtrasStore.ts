import { create } from "zustand";

interface TransportExtras {
  metronomeEnabled: boolean;
  countInEnabled: boolean;
  followPlayhead: boolean;
  set: (partial: Partial<TransportExtras>) => void;
}

export const useTransportExtrasStore = create<TransportExtras>((set) => ({
  metronomeEnabled: false,
  countInEnabled: false,
  followPlayhead: false,
  set: (p) => set(p),
}));
