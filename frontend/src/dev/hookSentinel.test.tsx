import React from "react";
import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { render } from "@testing-library/react";
import { withRenderCount, withHookSentinel } from "./hookSentinel";

const Probe = (p: { i?: number }) => <div data-testid="probe">probe {p.i ?? 0}</div>;

describe("withRenderCount", () => {
  beforeEach(() => {
    (window as any).__HDAW_RESET_RENDER_COUNTS?.();
    delete (window as any).HDAW_DEBUG_RENDERS;
  });

  afterEach(() => {
    delete (window as any).HDAW_DEBUG_RENDERS;
    vi.restoreAllMocks();
  });

  it("increments counter per render", () => {
    const Wrapped = withRenderCount(Probe, "Probe");
    const { rerender } = render(<Wrapped />);
    rerender(<Wrapped i={1} />);
    rerender(<Wrapped i={2} />);
    expect((window as any).__HDAW_GET_RENDER_COUNTS().Probe).toBeGreaterThanOrEqual(3);
  });

  it("does not warn when HDAW_DEBUG_RENDERS is off", () => {
    const Wrapped = withRenderCount(Probe, "Quiet");
    const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
    for (let i = 0; i < 15; i++) render(<Wrapped i={i} />);
    expect(warn).not.toHaveBeenCalled();
  });

  it("warns every 10 renders when HDAW_DEBUG_RENDERS is on", () => {
    (window as any).HDAW_DEBUG_RENDERS = true;
    const Wrapped = withRenderCount(Probe, "Loud");
    const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
    for (let i = 0; i < 15; i++) render(<Wrapped i={i} />);
    expect(warn).toHaveBeenCalled();
    expect(warn.mock.calls.some(c => String(c[0]).includes("Loud"))).toBe(true);
  });

  it("composes with withHookSentinel", () => {
    const Boom = () => { throw new Error("boom"); };
    const Wrapped = withHookSentinel(withRenderCount(Boom, "Boom"), "Boom");
    const spy = vi.spyOn(console, "error").mockImplementation(() => {});
    expect(() => render(<Wrapped />)).toThrow(/boom/);
    expect((window as any).__HDAW_GET_RENDER_COUNTS().Boom).toBeGreaterThanOrEqual(1);
    spy.mockRestore();
  });

  it("reset clears counts", () => {
    const Wrapped = withRenderCount(Probe, "Reset");
    render(<Wrapped />);
    (window as any).__HDAW_RESET_RENDER_COUNTS();
    expect((window as any).__HDAW_GET_RENDER_COUNTS()).toEqual({});
  });
});
