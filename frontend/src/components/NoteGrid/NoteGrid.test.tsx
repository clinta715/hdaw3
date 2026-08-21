import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import React from "react";
import { NoteSnapshot } from "../../rpc/types";
import { Props } from "./noteGridTypes";
import NoteGrid from "./NoteGrid";

vi.mock("../../rpc", () => ({ rpc: { call: vi.fn() } }));

vi.mock("../../hooks/useAutoScroll", () => ({
  useAutoScroll: () => ({ update: vi.fn(), stop: vi.fn() }),
}));

vi.mock("./useNoteGridDrag", () => ({
  useNoteGridDrag: () => ({ handleMouseMove: vi.fn(), handleMouseUp: vi.fn() }),
}));

vi.mock("./useNoteGridInteractions", () => ({
  useNoteGridInteractions: () => ({
    handleDoubleClick: vi.fn(),
    deleteSelected: vi.fn(),
    transposeSelected: vi.fn(),
    quantizeSelected: vi.fn(),
    humanizeSelected: vi.fn(),
    handleKeyDown: vi.fn(),
  }),
}));

vi.mock("./useNoteGridMarquee", () => ({
  useNoteGridMarquee: () => ({
    marquee: null,
    marqueeJustCompleted: { current: false },
    handleMarqueeStart: vi.fn(),
  }),
}));

vi.mock("./useNoteGridNoteMouseDown", () => ({
  useNoteGridNoteMouseDown: (opts: any) =>
    (e: React.MouseEvent, note: any) => {
      if (note) opts.setSelectedNoteIds(new Set([note.noteId]));
    },
}));

const { rpc: mockRpc } = await import("../../rpc");

function makeNote(overrides: Partial<NoteSnapshot> = {}): NoteSnapshot {
  return {
    noteId: 1,
    pitch: 60,
    velocity: 0.8,
    startBeat: 0,
    durationBeats: 1,
    chance: 1,
    repeatCount: 0,
    repeatRate: 0,
    repeatCurve: 0,
    occurrence: 0,
    recurrence: 0,
    noteGain: 1,
    notePan: 0,
    notePitch: 0,
    noteTimbre: 0,
    notePressure: 0,
    ...overrides,
  };
}

function makeProps(overrides: Partial<Props> = {}): Props {
  return {
    notes: [],
    rpc: mockRpc as any,
    clipId: 1,
    pixelsPerBeat: 80,
    ...overrides,
  };
}

afterEach(() => cleanup());

describe("NoteGrid", () => {
  it("renders 'No notes' when notes array is empty", () => {
    render(<NoteGrid {...makeProps()} />);
    expect(screen.getByText("No notes")).toBeTruthy();
  });

  it("renders note rectangles (.ng-note) when notes present", () => {
    const { container } = render(
      <NoteGrid {...makeProps({ notes: [makeNote({ noteId: 1 })] })} />
    );
    expect(container.querySelectorAll(".ng-note").length).toBe(1);
  });

  it("each note has .ng-note class", () => {
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [
            makeNote({ noteId: 1 }),
            makeNote({ noteId: 2 }),
            makeNote({ noteId: 3 }),
          ],
        })}
      />
    );
    const notes = container.querySelectorAll(".ng-note");
    expect(notes.length).toBe(3);
    notes.forEach((n) => {
      expect(n.classList.contains("ng-note")).toBe(true);
    });
  });

  it("clicking the grid calls onSelectionChange with empty set", () => {
    const onSelectionChange = vi.fn();
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [makeNote({ noteId: 1 })],
          onSelectionChange,
        })}
      />
    );
    const grid = container.querySelector(".note-grid")!;
    fireEvent.click(grid, { clientX: 0, clientY: 0 });
    expect(onSelectionChange).toHaveBeenCalledWith(new Set());
  });

  it("clicking a note calls onSelectionChange", () => {
    const onSelectionChange = vi.fn();
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [makeNote({ noteId: 42 })],
          onSelectionChange,
        })}
      />
    );
    const note = container.querySelector('[data-note-id="42"]')!;
    fireEvent.mouseDown(note);
    expect(onSelectionChange).toHaveBeenCalled();
  });

  it("selected notes get .ng-note--selected class", () => {
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [makeNote({ noteId: 1 })],
          selectedNoteIds: new Set([1]),
        })}
      />
    );
    const note = container.querySelector('[data-note-id="1"]')!;
    expect(note.classList.contains("ng-note--selected")).toBe(true);
  });

  it("ctrl+wheel on grid calls onZoom", () => {
    const onZoom = vi.fn();
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [makeNote({ noteId: 1 })],
          onZoom,
        })}
      />
    );
    const grid = container.querySelector(".note-grid")!;
    fireEvent.wheel(grid, { ctrlKey: true, deltaY: -100 });
    expect(onZoom).toHaveBeenCalled();
  });

  it("context menu opens on right-click of a note", () => {
    const { container } = render(
      <NoteGrid {...makeProps({ notes: [makeNote({ noteId: 7 })] })} />
    );
    const note = container.querySelector('[data-note-id="7"]')!;
    fireEvent.contextMenu(note, { clientX: 100, clientY: 200 });
    expect(container.querySelector(".ng-context-menu")).toBeTruthy();
  });

  it("context menu shows Quantize, Humanize, Transpose, Delete", () => {
    const { container } = render(
      <NoteGrid {...makeProps({ notes: [makeNote({ noteId: 7 })] })} />
    );
    const note = container.querySelector('[data-note-id="7"]')!;
    fireEvent.contextMenu(note, { clientX: 100, clientY: 200 });
    const items = container.querySelectorAll(".ng-context-item span:first-child");
    const labels = Array.from(items).map((el) => el.textContent);
    expect(labels).toContain("Quantize");
    expect(labels).toContain("Humanize");
    expect(labels).toContain("Transpose Up +1");
    expect(labels.some((l) => l && l.includes("Delete"))).toBe(true);
  });

  it("renders without crashing with empty notes", () => {
    const { container } = render(<NoteGrid {...makeProps({ notes: [] })} />);
    expect(container.querySelector(".note-grid")).toBeTruthy();
  });

  it("renders without crashing with notes", () => {
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [
            makeNote({ noteId: 1 }),
            makeNote({ noteId: 2 }),
            makeNote({ noteId: 3 }),
          ],
        })}
      />
    );
    expect(container.querySelectorAll(".ng-note").length).toBe(3);
  });

  it("note opacity is derived from velocity (higher velocity = higher opacity)", () => {
    const { container } = render(
      <NoteGrid
        {...makeProps({
          notes: [
            makeNote({ noteId: 1, velocity: 0.2 }),
            makeNote({ noteId: 2, velocity: 0.9 }),
          ],
        })}
      />
    );
    const lowVel = container.querySelector('[data-note-id="1"]')!;
    const highVel = container.querySelector('[data-note-id="2"]')!;
    const lowOpacity = parseFloat(lowVel.getAttribute("style")!.match(/opacity:\s*([\d.]+)/)![1]);
    const highOpacity = parseFloat(highVel.getAttribute("style")!.match(/opacity:\s*([\d.]+)/)![1]);
    expect(highOpacity).toBeGreaterThan(lowOpacity);
  });
});
