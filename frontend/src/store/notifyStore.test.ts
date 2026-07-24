import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { useNotifyStore, reportRpcError } from "../store/notifyStore";

describe("notifyStore", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useNotifyStore.getState().clear();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("starts with no toasts", () => {
    expect(useNotifyStore.getState().toasts).toEqual([]);
  });

  it("pushes an info toast", () => {
    useNotifyStore.getState().push({ level: "info", message: "Hello" });
    const toasts = useNotifyStore.getState().toasts;
    expect(toasts).toHaveLength(1);
    expect(toasts[0].level).toBe("info");
    expect(toasts[0].message).toBe("Hello");
  });

  it("pushes an error toast", () => {
    useNotifyStore.getState().push({ level: "error", message: "Oops" });
    const toasts = useNotifyStore.getState().toasts;
    expect(toasts).toHaveLength(1);
    expect(toasts[0].level).toBe("error");
  });

  it("pushes multiple toasts", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "One" });
    push({ level: "error", message: "Two" });
    push({ level: "success", message: "Three" });
    expect(useNotifyStore.getState().toasts).toHaveLength(3);
  });

  it("dismisses a toast by id", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "A" });
    push({ level: "info", message: "B" });
    const id = useNotifyStore.getState().toasts[0].id;
    useNotifyStore.getState().dismiss(id);
    expect(useNotifyStore.getState().toasts).toHaveLength(1);
    expect(useNotifyStore.getState().toasts[0].message).toBe("B");
  });

  it("clears all toasts", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "A" });
    push({ level: "error", message: "B" });
    useNotifyStore.getState().clear();
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
  });

  it("auto-dismisses info toast after 3s", () => {
    useNotifyStore.getState().push({ level: "info", message: "Temp" });
    expect(useNotifyStore.getState().toasts).toHaveLength(1);
    vi.advanceTimersByTime(3000);
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
  });

  it("auto-dismisses error toast after 6s", () => {
    useNotifyStore.getState().push({ level: "error", message: "Err" });
    vi.advanceTimersByTime(5999);
    expect(useNotifyStore.getState().toasts).toHaveLength(1);
    vi.advanceTimersByTime(1);
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
  });

  it("preserves non-expired toasts when one expires", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "Short" });
    push({ level: "info", message: "Long", ttl: 10000 });
    vi.advanceTimersByTime(3000);
    const remaining = useNotifyStore.getState().toasts;
    expect(remaining).toHaveLength(1);
    expect(remaining[0].message).toBe("Long");
  });

  it("dismiss cancels auto-dismiss timer", () => {
    useNotifyStore.getState().push({ level: "info", message: "A", ttl: 5000 });
    const id = useNotifyStore.getState().toasts[0].id;
    useNotifyStore.getState().dismiss(id);
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
    vi.advanceTimersByTime(10000);
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
  });

  it("clear cancels all auto-dismiss timers", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "A", ttl: 5000 });
    push({ level: "info", message: "B", ttl: 5000 });
    useNotifyStore.getState().clear();
    vi.advanceTimersByTime(10000);
    expect(useNotifyStore.getState().toasts).toHaveLength(0);
  });

  it("reportRpcError pushes an error toast", () => {
    reportRpcError("project.undo", new Error("timeout"));
    const toasts = useNotifyStore.getState().toasts;
    expect(toasts).toHaveLength(1);
    expect(toasts[0].level).toBe("error");
    expect(toasts[0].message).toContain("project.undo");
    expect(toasts[0].message).toContain("timeout");
  });

  it("reportRpcError handles non-Error values", () => {
    reportRpcError("transport.play", "something broke");
    const toasts = useNotifyStore.getState().toasts;
    expect(toasts).toHaveLength(1);
    expect(toasts[0].message).toContain("something broke");
  });

  it("toast with ttl=0 does not auto-dismiss", () => {
    useNotifyStore.getState().push({ level: "info", message: "Sticky", ttl: 0 });
    vi.advanceTimersByTime(60000);
    expect(useNotifyStore.getState().toasts).toHaveLength(1);
  });
});
