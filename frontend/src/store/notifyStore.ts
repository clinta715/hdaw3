import { create } from "zustand";

// Toast notifications for surfacing failures (and occasional success/info
// messages) to the user. The motivating use case: before this store existed,
// every failed rpc.call was caught with `.catch(() => {})` or
// `.catch(console.error)`, so dead RPC methods (export.audio, transport.record,
// project.setClipGainEnvelope, etc.) failed silently — the UI looked like it
// worked. Showing a toast on failure turns those into visible error messages
// and makes regressions immediately obvious.

export type ToastLevel = "error" | "info" | "success";

export interface Toast {
  id: number;
  level: ToastLevel;
  message: string;
  method?: string;
  ttl?: number;
}

interface NotifyState {
  toasts: Toast[];
  push: (t: Omit<Toast, "id"> & Partial<Pick<Toast, "id">>) => void;
  dismiss: (id: number) => void;
  clear: () => void;
}

let nextId = 1;
// Track pending auto-dismiss timers so dismiss/clear can cancel them.
// Without this, a flurry of toasts (e.g. RPC errors) leaves an unbounded
// number of armed setTimeouts whose harmless filter callbacks still run later.
const timers = new Map<number, ReturnType<typeof setTimeout>>();

export const useNotifyStore = create<NotifyState>((set) => ({
  toasts: [],
  push: (t) => {
    const id = t.id ?? nextId++;
    const ttl = t.ttl ?? (t.level === "error" ? 6000 : 3000);
    const toast: Toast = { id, level: t.level, message: t.message, method: t.method, ttl };
    set((s) => ({ toasts: [...s.toasts, toast] }));
    if (ttl > 0) {
      const handle = setTimeout(() => {
        timers.delete(id);
        set((s) => ({ toasts: s.toasts.filter((x) => x.id !== id) }));
      }, ttl);
      timers.set(id, handle);
    }
  },
  dismiss: (id) => {
    const handle = timers.get(id);
    if (handle) {
      clearTimeout(handle);
      timers.delete(id);
    }
    set((s) => ({ toasts: s.toasts.filter((x) => x.id !== id) }));
  },
  clear: () => {
    for (const handle of timers.values()) clearTimeout(handle);
    timers.clear();
    set({ toasts: [] });
  },
}));

// Convenience helpers. `reportRpcError` is what most catch handlers should
// call: it formats a useful message and pushes an error toast.
export function reportRpcError(method: string, err: unknown) {
  const detail = err instanceof Error ? err.message : String(err);
  useNotifyStore.getState().push({
    level: "error",
    method,
    message: `${method} failed: ${detail}`,
  });
}
