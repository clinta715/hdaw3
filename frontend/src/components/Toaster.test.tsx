import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/react";
import Toaster from "./Toaster";
import { useNotifyStore } from "../store/notifyStore";

describe("Toaster", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useNotifyStore.getState().clear();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("renders nothing when no toasts", () => {
    const { container } = render(<Toaster />);
    expect(container.innerHTML).toBe("");
  });

  it("renders a single toast", () => {
    useNotifyStore.getState().push({ level: "info", message: "Hello world", ttl: 0 });
    render(<Toaster />);
    expect(screen.getByText("Hello world")).toBeInTheDocument();
  });

  it("renders multiple toasts", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "One", ttl: 0 });
    push({ level: "error", message: "Two", ttl: 0 });
    push({ level: "success", message: "Three", ttl: 0 });
    render(<Toaster />);
    expect(screen.getByText("One")).toBeInTheDocument();
    expect(screen.getByText("Two")).toBeInTheDocument();
    expect(screen.getByText("Three")).toBeInTheDocument();
  });

  it("applies error class for error toasts", () => {
    useNotifyStore.getState().push({ level: "error", message: "Error!", ttl: 0 });
    render(<Toaster />);
    const toast = screen.getByText("Error!").closest(".toast");
    expect(toast?.className).toContain("toast--error");
  });

  it("applies info class for info toasts", () => {
    useNotifyStore.getState().push({ level: "info", message: "Info!", ttl: 0 });
    render(<Toaster />);
    const toast = screen.getByText("Info!").closest(".toast");
    expect(toast?.className).toContain("toast--info");
  });

  it("applies success class for success toasts", () => {
    useNotifyStore.getState().push({ level: "success", message: "Done!", ttl: 0 });
    render(<Toaster />);
    const toast = screen.getByText("Done!").closest(".toast");
    expect(toast?.className).toContain("toast--success");
  });

  it("dismisses toast on close button click", () => {
    useNotifyStore.getState().push({ level: "info", message: "Dismiss me", ttl: 0 });
    render(<Toaster />);
    expect(screen.getByText("Dismiss me")).toBeInTheDocument();
    fireEvent.click(screen.getByTitle("Dismiss"));
    expect(screen.queryByText("Dismiss me")).not.toBeInTheDocument();
  });

  it("dismisses only the clicked toast", () => {
    const { push } = useNotifyStore.getState();
    push({ level: "info", message: "Keep", ttl: 0 });
    push({ level: "info", message: "Remove", ttl: 0 });
    render(<Toaster />);
    const closeButtons = screen.getAllByTitle("Dismiss");
    fireEvent.click(closeButtons[1]);
    expect(screen.getByText("Keep")).toBeInTheDocument();
    expect(screen.queryByText("Remove")).not.toBeInTheDocument();
  });

  it("removes toast when auto-dismiss timer fires", () => {
    useNotifyStore.getState().push({ level: "info", message: "Auto", ttl: 1000 });
    render(<Toaster />);
    expect(screen.getByText("Auto")).toBeInTheDocument();
    act(() => {
      vi.advanceTimersByTime(1000);
    });
    expect(screen.queryByText("Auto")).not.toBeInTheDocument();
  });
});
