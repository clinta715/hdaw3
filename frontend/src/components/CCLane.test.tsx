import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup, screen, fireEvent, act } from "@testing-library/react";
import CCLane from "./CCLane";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

const defaultProps = {
  clipId: 1,
  controllerNumber: 7,
  width: 300,
  pixelsPerBeat: 40,
  scrollX: 0,
};

const samplePoints = [
  { ccId: 1, controllerNumber: 7, beat: 0, value: 64 },
  { ccId: 2, controllerNumber: 7, beat: 2, value: 100 },
  { ccId: 3, controllerNumber: 7, beat: 4, value: 40 },
];

describe("CCLane", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue([]);
  });

  afterEach(() => cleanup());

  it("renders without crashing", async () => {
    await act(async () => {
      render(<CCLane {...defaultProps} />);
    });
    expect(screen.getByText("CC7")).toBeTruthy();
  });

  it("renders a canvas element", async () => {
    const { container } = render(<CCLane {...defaultProps} />);
    await act(async () => {});
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("displays the CC number label", async () => {
    await act(async () => {
      render(<CCLane {...defaultProps} controllerNumber={1} />);
    });
    expect(screen.getByText("CC1")).toBeTruthy();
  });

  it("fetches CC points on mount via read.getCcPoints", async () => {
    await act(async () => {
      render(<CCLane {...defaultProps} />);
    });
    expect(mockedCall).toHaveBeenCalledWith("read.getCcPoints", {
      clipId: 1,
      controllerNumber: 7,
    });
  });

  it("renders without crashing with existing CC points", async () => {
    mockedCall.mockResolvedValue(samplePoints);
    await act(async () => {
      render(<CCLane {...defaultProps} />);
    });
    expect(screen.getByText("CC7")).toBeTruthy();
    expect(mockedCall).toHaveBeenCalled();
  });

  it("shows remove button when onRemove is provided", async () => {
    const onRemove = vi.fn();
    await act(async () => {
      render(<CCLane {...defaultProps} onRemove={onRemove} />);
    });
    expect(screen.getByText("×")).toBeTruthy();
  });

  it("clicking remove calls onRemove", async () => {
    const onRemove = vi.fn();
    await act(async () => {
      render(<CCLane {...defaultProps} onRemove={onRemove} />);
    });
    fireEvent.click(screen.getByText("×"));
    expect(onRemove).toHaveBeenCalledTimes(1);
  });

  it("collapse toggle hides the canvas", async () => {
    const { container } = render(<CCLane {...defaultProps} />);
    await act(async () => {});
    expect(container.querySelector("canvas")).toBeTruthy();
    fireEvent.click(screen.getByText("CC7"));
    expect(container.querySelector("canvas")).toBeNull();
  });

  it("second click on toggle restores canvas", async () => {
    const { container } = render(<CCLane {...defaultProps} />);
    await act(async () => {});
    fireEvent.click(screen.getByText("CC7"));
    expect(container.querySelector("canvas")).toBeNull();
    fireEvent.click(screen.getByText("CC7"));
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("canvas has correct width", async () => {
    const { container } = render(<CCLane {...defaultProps} width={500} />);
    await act(async () => {});
    const canvas = container.querySelector("canvas") as HTMLCanvasElement;
    expect(canvas).toBeTruthy();
    expect(canvas.width).toBe(500);
  });
});
