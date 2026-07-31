import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup } from "@testing-library/react";
import GainEnvelopeEditor from "./GainEnvelopeEditor";
import { useUiStore } from "../store/uiStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

describe("GainEnvelopeEditor", () => {
  beforeEach(() => {
    useUiStore.setState({ snapEnabled: false, snapDivision: 4 });
  });

  afterEach(() => cleanup());

  it("renders a canvas element", () => {
    const { container } = render(
      <GainEnvelopeEditor clipId={1} points={[]} durationBeats={4} />
    );
    const canvas = container.querySelector(".gee-canvas");
    expect(canvas).toBeInTheDocument();
    expect(canvas?.tagName).toBe("CANVAS");
  });

  it("canvas has crosshair cursor", () => {
    const { container } = render(
      <GainEnvelopeEditor clipId={1} points={[]} durationBeats={4} />
    );
    const canvas = container.querySelector(".gee-canvas") as HTMLElement;
    expect(canvas.style.cursor).toBe("crosshair");
  });

  it("canvas has fixed height of 80px", () => {
    const { container } = render(
      <GainEnvelopeEditor clipId={1} points={[]} durationBeats={4} />
    );
    const canvas = container.querySelector(".gee-canvas") as HTMLElement;
    expect(canvas.style.height).toBe("80px");
  });

  it("renders without crashing with explicit points", () => {
    const points = [
      { time: 0, gain: 1 },
      { time: 2, gain: 0.5 },
      { time: 4, gain: 1 },
    ];
    const { container } = render(
      <GainEnvelopeEditor clipId={1} points={points} durationBeats={4} />
    );
    expect(container.querySelector(".gee-canvas")).toBeInTheDocument();
  });

  it("renders without crashing with single point", () => {
    const { container } = render(
      <GainEnvelopeEditor clipId={1} points={[{ time: 0, gain: 1 }]} durationBeats={4} />
    );
    expect(container.querySelector(".gee-canvas")).toBeInTheDocument();
  });
});
