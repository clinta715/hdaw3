import { create } from "zustand";

interface TransportExtras {
  metronomeEnabled: boolean;
  countInEnabled: boolean;
  followPlayhead: boolean;
  timeSignatureNum: number;
  timeSignatureDen: number;
  set: (partial: Partial<TransportExtras>) => void;
}

export const useTransportExtrasStore = create<TransportExtras>((set) => ({
  metronomeEnabled: false,
  countInEnabled: false,
  followPlayhead: false,
  // TODO: Sync time signature from backend when TransportSnapshot includes it
  timeSignatureNum: 4,
  timeSignatureDen: 4,
  set: (p) => set(p),
}));
