import { describe, it, expect, vi, afterEach } from "vitest";
import { render, cleanup, fireEvent } from "@testing-library/react";
import VelocityLane from "./VelocityLane";
import type { NoteSnapshot } from "../rpc/types";
import type { RpcClient } from "../rpc/client";

function mockRpc(): RpcClient {
  return { call: vi.fn(async () => null) } as unknown as RpcClient;
}

function mkNote(overrides: Partial<NoteSnapshot> = {}): NoteSnapshot {
  return {
    noteId: 1,
    pitch: 60,
    velocity: 100,
    startBeat: 0,
    durationBeats: 1,
    chance: 1,
    repeatCount: 0,
    repeatRate: 0.25,
    repeatCurve: 0,
    occurrence: 0,
    recurrence: 0,
    noteGain: 1,
    notePan: 0,
    notePitch: 0,
    noteTimbre: 0.5,
    notePressure: 0,
    ...overrides,
  };
}

describe("VelocityLane", () => {
  afterEach(() => cleanup());

  it("renders without crashing with empty notes", () => {
    const { container } = render(
      <VelocityLane
        notes={[]}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    expect(container.querySelector(".velocity-lane")).not.toBeNull();
  });

  it("renders without crashing with notes", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 80, startBeat: 0 }),
      mkNote({ noteId: 2, velocity: 120, startBeat: 2 }),
    ];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    expect(container.querySelectorAll(".velocity-bar").length).toBe(2);
  });

  it("renders a velocity bar for each note", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 60, startBeat: 0 }),
      mkNote({ noteId: 2, velocity: 90, startBeat: 1 }),
      mkNote({ noteId: 3, velocity: 110, startBeat: 3 }),
    ];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const bars = container.querySelectorAll(".velocity-bar");
    expect(bars.length).toBe(3);
  });

  it("velocity bar height is proportional to velocity", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 64, startBeat: 0 }),
    ];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const bar = container.querySelector(".velocity-bar") as HTMLElement;
    const height = parseFloat(bar.style.height);
    const expected = (64 / 127) * (40 - 4);
    expect(height).toBeCloseTo(expected, 1);
  });

  it("selected notes get --selected modifier class", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 100, startBeat: 0 }),
      mkNote({ noteId: 2, velocity: 80, startBeat: 1 }),
    ];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set([1])}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const bars = container.querySelectorAll(".velocity-bar");
    expect(bars[0].classList.contains("velocity-bar--selected")).toBe(true);
    expect(bars[1].classList.contains("velocity-bar--selected")).toBe(false);
  });

  it("dragging a velocity bar calls onVelocityChange", () => {
    const notes = [mkNote({ noteId: 1, velocity: 80, startBeat: 0 })];
    const onVelocityChange = vi.fn();
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={onVelocityChange}
      />,
    );
    const bar = container.querySelector(".velocity-bar") as HTMLElement;
    fireEvent.mouseDown(bar, { clientY: 200 });
    fireEvent.mouseMove(window, { clientY: 180 });
    expect(onVelocityChange).toHaveBeenCalled();
  });

  it("lane renders with notes spanning correct width", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 100, startBeat: 0 }),
      mkNote({ noteId: 2, velocity: 100, startBeat: 10 }),
    ];
    const pixelsPerBeat = 80;
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={pixelsPerBeat}
        onVelocityChange={vi.fn()}
      />,
    );
    const bars = container.querySelectorAll(".velocity-bar");
    const bar1Left = parseFloat((bars[0] as HTMLElement).style.left);
    const bar2Left = parseFloat((bars[1] as HTMLElement).style.left);
    expect(bar1Left).toBe(0);
    expect(bar2Left).toBeCloseTo(10 * pixelsPerBeat, 0);
  });

  it("handles notes at the same beat position", () => {
    const notes = [
      mkNote({ noteId: 1, velocity: 100, startBeat: 2 }),
      mkNote({ noteId: 2, velocity: 80, startBeat: 2 }),
    ];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const bars = container.querySelectorAll(".velocity-bar");
    expect(bars.length).toBe(2);
  });

  it("renders with zero velocity notes without crashing", () => {
    const notes = [mkNote({ noteId: 1, velocity: 0, startBeat: 0 })];
    const { container } = render(
      <VelocityLane
        notes={notes}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const bar = container.querySelector(".velocity-bar") as HTMLElement;
    expect(bar).not.toBeNull();
    expect(parseFloat(bar.style.height)).toBeCloseTo(0, 1);
  });

  it("lane container has a fixed height style", () => {
    const { container } = render(
      <VelocityLane
        notes={[]}
        selectedNoteIds={new Set()}
        rpc={mockRpc()}
        pixelsPerBeat={50}
        onVelocityChange={vi.fn()}
      />,
    );
    const lane = container.querySelector(".velocity-lane") as HTMLElement;
    expect(lane).not.toBeNull();
    const computed = window.getComputedStyle(lane);
    expect(computed.height).toBe("40px");
  });
});
