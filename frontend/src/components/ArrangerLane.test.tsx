import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import React from "react";
import { ArrangerLane } from "./ArrangerLane";
import { useArrangerStore } from "../store/arrangerStore";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

const defaultProps = { pps: 40, scrollLeft: 0 };

const makeRegion = (
  overrides: Partial<{
    regionID: string;
    name: string;
    startTime: number;
    duration: number;
    color: number;
  }> = {}
) => ({
  regionID: "r1",
  name: "Region 1",
  startTime: 0,
  duration: 4,
  color: 0xff000000,
  ...overrides,
});

beforeEach(() => {
  mockedCall.mockReset();
  mockedCall.mockResolvedValue(undefined);
  useArrangerStore.setState({ regions: [], chains: [] });
});

afterEach(() => {
  cleanup();
  useArrangerStore.setState({ regions: [], chains: [] });
});

describe("ArrangerLane", () => {
  // ── 1. Region Rendering ──

  it("renders without crashing with regions", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    render(<ArrangerLane {...defaultProps} />);
    expect(screen.getByText("Region 1")).toBeInTheDocument();
  });

  it("shows region name", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ name: "Verse" })],
    });
    render(<ArrangerLane {...defaultProps} />);
    expect(screen.getByText("Verse")).toBeInTheDocument();
  });

  it("renders region with correct position via inline style", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 2, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    expect(region).toBeTruthy();
    expect(region.style.left).toBe("80px");
    expect(region.style.width).toBe("160px");
  });

  it("applies backgroundColor from region color", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ color: 0xff00ff00 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    expect(region.style.backgroundColor).toBeTruthy();
  });

  it("uses accent fallback when color is zero", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ color: 0 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    expect(region.style.backgroundColor).toBe("var(--accent)");
  });

  it("renders multiple regions", () => {
    useArrangerStore.setState({
      regions: [
        makeRegion({ regionID: "r1", name: "A", startTime: 0, duration: 4 }),
        makeRegion({ regionID: "r2", name: "B", startTime: 4, duration: 4 }),
        makeRegion({ regionID: "r3", name: "C", startTime: 8, duration: 4 }),
      ],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const regions = container.querySelectorAll(".tl-arranger-region");
    expect(regions).toHaveLength(3);
  });

  it("adjusts left position based on scrollLeft", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 4, duration: 2 })],
    });
    const { container } = render(
      <ArrangerLane pps={40} scrollLeft={80} />
    );
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    expect(region.style.left).toBe("80px");
  });

  // ── 2. Selection ──

  it("clicking a region selects it", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    fireEvent.mouseDown(region);
    expect(region.classList.contains("selected")).toBe(true);
  });

  it("clicking a different region switches selection", () => {
    useArrangerStore.setState({
      regions: [
        makeRegion({ regionID: "r1", name: "A" }),
        makeRegion({ regionID: "r2", name: "B", startTime: 4 }),
      ],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const regions = container.querySelectorAll(".tl-arranger-region");

    fireEvent.mouseDown(regions[0]);
    expect(regions[0].classList.contains("selected")).toBe(true);

    fireEvent.mouseDown(regions[1]);
    expect(regions[1].classList.contains("selected")).toBe(true);
  });

  it("clicking the lane background deselects", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region);
    expect(region.classList.contains("selected")).toBe(true);

    fireEvent.click(lane, { target: lane });
    expect(region.classList.contains("selected")).toBe(false);
  });

  it("selected region has the selected CSS class", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;
    expect(region.classList.contains("selected")).toBe(false);
    fireEvent.mouseDown(region);
    expect(region.classList.contains("selected")).toBe(true);
  });

  // ── 3. Drag to Move ──

  it("mouse move during drag calls setArrangerRegionBounds", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 0, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region, { clientX: 80 });
    fireEvent.mouseMove(lane, { clientX: 120 });

    expect(mockedCall).toHaveBeenCalledWith("project.setArrangerRegionBounds", {
      regionID: "r1",
      startTime: 1,
      duration: 4,
    });
  });

  it("mouse move snaps to quarter-beat grid", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 0, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region, { clientX: 80 });
    fireEvent.mouseMove(lane, { clientX: 95 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[0]).toBe("project.setArrangerRegionBounds");
    const startTime = lastCall[1].startTime;
    expect(startTime % 0.25).toBe(0);
  });

  it("mouse move clamps start to non-negative", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 1, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region, { clientX: 40 });
    fireEvent.mouseMove(lane, { clientX: 0 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[1].startTime).toBeGreaterThanOrEqual(0);
  });

  it("mouse up clears drag state", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region, { clientX: 80 });
    fireEvent.mouseUp(lane);
    mockedCall.mockClear();

    // After mouseUp, a new mouseDown without move should not trigger bounds RPC
    fireEvent.mouseDown(region, { clientX: 80 });
    expect(mockedCall).not.toHaveBeenCalledWith(
      "project.setArrangerRegionBounds",
      expect.anything()
    );
  });

  // ── 4. Resize ──

  it("left handle resize calls setArrangerRegionBounds", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 2, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const handle = container.querySelector(
      ".tl-arranger-region-handle-left"
    ) as HTMLElement;

    fireEvent.mouseDown(handle, { clientX: 80 });
    fireEvent.mouseMove(lane, { clientX: 120 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[0]).toBe("project.setArrangerRegionBounds");
    expect(lastCall[1].regionID).toBe("r1");
  });

  it("right handle resize calls setArrangerRegionBounds", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 0, duration: 4 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const handle = container.querySelector(
      ".tl-arranger-region-handle-right"
    ) as HTMLElement;

    fireEvent.mouseDown(handle, { clientX: 80 });
    fireEvent.mouseMove(lane, { clientX: 120 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[0]).toBe("project.setArrangerRegionBounds");
    expect(lastCall[1].duration).toBe(5);
  });

  it("right handle enforces minimum duration of 0.25", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 0, duration: 2 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const handle = container.querySelector(
      ".tl-arranger-region-handle-right"
    ) as HTMLElement;

    fireEvent.mouseDown(handle, { clientX: 200 });
    // Drag far left to try to shrink below minimum
    fireEvent.mouseMove(lane, { clientX: 0 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[1].duration).toBeGreaterThanOrEqual(0.25);
  });

  it("left handle resize enforces minimum duration of 0.25", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ startTime: 0, duration: 2 })],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;
    const handle = container.querySelector(
      ".tl-arranger-region-handle-left"
    ) as HTMLElement;

    fireEvent.mouseDown(handle, { clientX: 0 });
    // Drag far right to try to shrink from left
    fireEvent.mouseMove(lane, { clientX: 160 });

    const lastCall = mockedCall.mock.calls[mockedCall.mock.calls.length - 1];
    expect(lastCall[1].duration).toBeGreaterThanOrEqual(0.25);
  });

  // ── 5. Rename ──

  it("double-click opens rename input", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    expect(input).toBeTruthy();
    expect(input.value).toBe("Region 1");
  });

  it("enter commits new name via RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    fireEvent.change(input, { target: { value: "New Name" } });
    fireEvent.keyDown(input, { key: "Enter" });

    expect(mockedCall).toHaveBeenCalledWith("project.setArrangerRegionName", {
      regionID: "r1",
      name: "New Name",
    });
  });

  it("escape cancels rename without RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    fireEvent.change(input, { target: { value: "Abandoned" } });
    fireEvent.keyDown(input, { key: "Escape" });

    expect(mockedCall).not.toHaveBeenCalledWith(
      "project.setArrangerRegionName",
      expect.anything()
    );
  });

  it("blurring the input commits the name", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    fireEvent.change(input, { target: { value: "Blurred Name" } });
    fireEvent.blur(input);

    expect(mockedCall).toHaveBeenCalledWith("project.setArrangerRegionName", {
      regionID: "r1",
      name: "Blurred Name",
    });
  });

  it("whitespace-only name does not call RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    fireEvent.change(input, { target: { value: "   " } });
    fireEvent.keyDown(input, { key: "Enter" });

    expect(mockedCall).not.toHaveBeenCalledWith(
      "project.setArrangerRegionName",
      expect.anything()
    );
  });

  it("trims whitespace from name", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.doubleClick(region);

    const input = container.querySelector(
      ".tl-arranger-region input"
    ) as HTMLInputElement;
    fireEvent.change(input, { target: { value: "  Trimmed  " } });
    fireEvent.keyDown(input, { key: "Enter" });

    expect(mockedCall).toHaveBeenCalledWith("project.setArrangerRegionName", {
      regionID: "r1",
      name: "Trimmed",
    });
  });

  // ── 6. Delete ──

  it("Delete key removes selected region via RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region);
    fireEvent.keyDown(region, { key: "Delete" });

    expect(mockedCall).toHaveBeenCalledWith("project.removeArrangerRegion", {
      regionID: "r1",
    });
  });

  it("Backspace key removes selected region via RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const region = container.querySelector(".tl-arranger-region") as HTMLElement;

    fireEvent.mouseDown(region);
    fireEvent.keyDown(region, { key: "Backspace" });

    expect(mockedCall).toHaveBeenCalledWith("project.removeArrangerRegion", {
      regionID: "r1",
    });
  });

  it("Delete without selection does not call RPC", () => {
    useArrangerStore.setState({ regions: [makeRegion()] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;

    fireEvent.keyDown(lane, { key: "Delete" });

    expect(mockedCall).not.toHaveBeenCalledWith(
      "project.removeArrangerRegion",
      expect.anything()
    );
  });

  // ── 7. Empty State ──

  it("shows empty lane when no regions", () => {
    useArrangerStore.setState({ regions: [] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const emptyLane = container.querySelector(".tl-arranger-lane-empty");
    expect(emptyLane).toBeTruthy();
  });

  it("empty lane has zero height via CSS class", () => {
    useArrangerStore.setState({ regions: [] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const emptyLane = container.querySelector(".tl-arranger-lane-empty") as HTMLElement;
    expect(emptyLane).toBeTruthy();
    expect(emptyLane.classList.contains("tl-arranger-lane-empty")).toBe(true);
  });

  it("double-click on empty lane calls addArrangerRegion", () => {
    useArrangerStore.setState({ regions: [] });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const emptyLane = container.querySelector(".tl-arranger-lane-empty") as HTMLElement;

    fireEvent.doubleClick(emptyLane);

    expect(mockedCall).toHaveBeenCalledWith("project.addArrangerRegion", {
      name: "Section 1",
      startTime: expect.any(Number),
      duration: 8,
    });
  });

  it("double-click empty lane with existing regions increments name", () => {
    useArrangerStore.setState({
      regions: [
        makeRegion({ regionID: "r1" }),
        makeRegion({ regionID: "r2", startTime: 4 }),
      ],
    });
    const { container } = render(<ArrangerLane {...defaultProps} />);
    const lane = container.querySelector(".tl-arranger-lane") as HTMLElement;

    fireEvent.doubleClick(lane);

    expect(mockedCall).toHaveBeenCalledWith("project.addArrangerRegion", {
      name: "Section 3",
      startTime: expect.any(Number),
      duration: 8,
    });
  });
});
