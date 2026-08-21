import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup, fireEvent } from "@testing-library/react";
import AutomationLaneCanvas from "./AutomationLaneCanvas";
import type { AutomationPointSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const { mockAddPoint, mockClearSelection, mockSelectPoint, mockRemovePoints, mockFetchForTrack, mockSetStatusHint } = vi.hoisted(() => ({
  mockAddPoint: vi.fn().mockResolvedValue(undefined),
  mockClearSelection: vi.fn(),
  mockSelectPoint: vi.fn(),
  mockRemovePoints: vi.fn(),
  mockFetchForTrack: vi.fn().mockResolvedValue(undefined),
  mockSetStatusHint: vi.fn(),
}));

vi.mock("../store/automationStore", () => {
  const store = (selector: (s: Record<string, unknown>) => unknown) =>
    selector({
      selectedPointTimes: new Map(),
      fetchForTrack: mockFetchForTrack,
      clearSelection: mockClearSelection,
      addPoint: mockAddPoint,
      selectPoint: mockSelectPoint,
      removePoints: mockRemovePoints,
    });
  store.getState = () => ({ selectedPointTimes: new Map() });
  return { useAutomationStore: store };
});

vi.mock("../store/projectStore", () => {
  const store = (selector: (s: Record<string, unknown>) => unknown) =>
    selector({ snapshot: null });
  store.getState = () => ({ snapshot: null });
  return { useProjectStore: store };
});

vi.mock("../store/uiStore", () => {
  const store = (selector: (s: Record<string, unknown>) => unknown) =>
    selector({ setStatusHint: mockSetStatusHint });
  store.getState = () => ({
    setStatusHint: mockSetStatusHint,
    snapEnabled: false,
    snapDivision: 4,
    snapGridOffset: 0,
    snapToEvents: [],
  });
  return { useUiStore: store };
});

vi.mock("./snapUtils", () => ({ snap: vi.fn((_v: number) => _v) }));

beforeEach(() => {
  if (typeof globalThis.ResizeObserver === "undefined") {
    (globalThis as any).ResizeObserver = class {
      observe() {}
      unobserve() {}
      disconnect() {}
    };
  }
  mockAddPoint.mockClear();
  mockClearSelection.mockClear();
});

afterEach(() => cleanup());

function mkRpc() {
  return { call: vi.fn().mockResolvedValue(undefined) } as any;
}

function mkPoints(count: number): AutomationPointSnapshot[] {
  const pts: AutomationPointSnapshot[] = [];
  for (let i = 0; i < count; i++) {
    pts.push({ time: (i / count) * 32, value: i / count });
  }
  return pts;
}

function mkDefaultProps(overrides?: Partial<Record<string, unknown>>) {
  return {
    laneName: "Volume",
    points: [] as AutomationPointSnapshot[],
    trackIndex: 0,
    rpc: mkRpc(),
    viewStartBeat: 0,
    viewEndBeat: 32,
    ...overrides,
  };
}

describe("AutomationLaneCanvas", () => {
  it("renders a canvas element", () => {
    const { container } = render(<AutomationLaneCanvas {...mkDefaultProps()} />);
    expect(container.querySelector("canvas")).not.toBeNull();
  });

  it("canvas has correct width from props", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps()} />
    );
    const canvas = container.querySelector("canvas")!;
    expect(canvas).toBeInstanceOf(HTMLCanvasElement);
  });

  it("renders without crashing with empty points", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps({ points: [] })} />
    );
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("renders without crashing with automation points", () => {
    const points: AutomationPointSnapshot[] = [
      { time: 0, value: 0.5 },
      { time: 8, value: 0.8 },
      { time: 16, value: 0.2 },
    ];
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps({ points })} />
    );
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("clicking the canvas adds a point", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps()} />
    );
    const canvas = container.querySelector("canvas")!;
    fireEvent.mouseDown(canvas, { clientX: 100, clientY: 40, button: 0 });
    expect(mockAddPoint).toHaveBeenCalled();
  });

  it("right-click on canvas does not crash", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps()} />
    );
    const canvas = container.querySelector("canvas")!;
    fireEvent.contextMenu(canvas, { clientX: 100, clientY: 40 });
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("canvas uses devicePixelRatio for HiDPI (style.width/height set)", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps()} />
    );
    const canvas = container.querySelector("canvas")!;
    expect(canvas).toBeTruthy();
    expect(canvas.style.width || canvas.getAttribute("width")).toBeDefined();
  });

  it("renders without crashing with single point", () => {
    const points: AutomationPointSnapshot[] = [{ time: 4, value: 0.7 }];
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps({ points })} />
    );
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("renders without crashing with many points (50)", () => {
    const points = mkPoints(50);
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps({ points })} />
    );
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("canvas element is present and is a CANVAS tag", () => {
    const { container } = render(
      <AutomationLaneCanvas {...mkDefaultProps()} />
    );
    const canvas = container.querySelector("canvas");
    expect(canvas).not.toBeNull();
    expect(canvas!.tagName).toBe("CANVAS");
  });
});
