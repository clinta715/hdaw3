import { describe, it, expect, vi, afterEach } from "vitest";
import { render, screen, cleanup } from "@testing-library/react";
import { ErrorBoundary } from "./ErrorBoundary";
import React from "react";

afterEach(() => cleanup());

function ThrowingChild() {
  throw new Error("Test error");
}

function SafeChild() {
  return <div>Safe content</div>;
}

describe("ErrorBoundary", () => {
  it("renders children normally when no error", () => {
    render(
      <ErrorBoundary>
        <SafeChild />
      </ErrorBoundary>,
    );
    expect(screen.getByText("Safe content")).toBeDefined();
  });

  it("catches errors thrown by child components", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    render(
      <ErrorBoundary>
        <ThrowingChild />
      </ErrorBoundary>,
    );
    expect(screen.getByText("HDAW hit a render error")).toBeDefined();
    consoleSpy.mockRestore();
  });

  it("shows error message in the fallback UI", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    render(
      <ErrorBoundary>
        <ThrowingChild />
      </ErrorBoundary>,
    );
    expect(screen.getByText("Test error")).toBeDefined();
    consoleSpy.mockRestore();
  });

  it("shows a Reload button", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    render(
      <ErrorBoundary>
        <ThrowingChild />
      </ErrorBoundary>,
    );
    expect(screen.getByText("Reload")).toBeDefined();
    consoleSpy.mockRestore();
  });

  it("Reload button calls window.location.reload", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    const reloadSpy = vi.fn();
    Object.defineProperty(window, "location", {
      value: { reload: reloadSpy },
      writable: true,
    });
    render(
      <ErrorBoundary>
        <ThrowingChild />
      </ErrorBoundary>,
    );
    screen.getByText("Reload").click();
    expect(reloadSpy).toHaveBeenCalled();
    consoleSpy.mockRestore();
  });

  it("renders a custom fallback when provided", () => {
    const consoleSpy = vi.spyOn(console, "error").mockImplementation(() => {});
    render(
      <ErrorBoundary fallback={<div>Custom fallback</div>}>
        <ThrowingChild />
      </ErrorBoundary>,
    );
    expect(screen.getByText("Custom fallback")).toBeDefined();
    expect(screen.queryByText("HDAW hit a render error")).toBeNull();
    consoleSpy.mockRestore();
  });
});
