import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import NoteOperatorsPane from "./NoteOperatorsPane";
import type { RpcClient } from "../rpc/client";
import type { NoteSnapshot } from "../rpc/types";

function mockRpc(): RpcClient {
  const call = vi.fn(async () => null);
  return { call } as unknown as RpcClient;
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

describe("NoteOperatorsPane", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => cleanup());

  it("renders collapsed when localStorage says so", () => {
    localStorage.setItem("noteOperatorsCollapsed", "true");
    const rpc = mockRpc();
    const notes = [mkNote()];
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={notes}
        activeClip={{ clipId: 10, seed: 0 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    expect(screen.getByText("Note Operators")).toBeInTheDocument();
    const body = document.querySelector(".nop-body--collapsed");
    expect(body).toBeInTheDocument();
  });

  it("renders expanded by default", () => {
    const rpc = mockRpc();
    const notes = [mkNote()];
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={notes}
        activeClip={{ clipId: 10, seed: 0 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    expect(screen.getByText("Operators")).toBeInTheDocument();
    expect(screen.getByText("Expression")).toBeInTheDocument();
  });

  it("shows operator fields", () => {
    const rpc = mockRpc();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote()]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    expect(screen.getByText("Chance")).toBeInTheDocument();
    expect(screen.getByText("Repeat Cnt")).toBeInTheDocument();
    expect(screen.getByText("Repeat Rate")).toBeInTheDocument();
    expect(screen.getByText("Repeat Curve")).toBeInTheDocument();
    expect(screen.getByText("Occurrence")).toBeInTheDocument();
    expect(screen.getByText("Recurrence")).toBeInTheDocument();
    expect(screen.getByText("Gain")).toBeInTheDocument();
    expect(screen.getByText("Pan")).toBeInTheDocument();
    expect(screen.getByText("Pitch")).toBeInTheDocument();
    expect(screen.getByText("Timbre")).toBeInTheDocument();
    expect(screen.getByText("Pressure")).toBeInTheDocument();
  });

  it("shows Clip Seed input", () => {
    const rpc = mockRpc();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote()]}
        activeClip={{ clipId: 10, seed: 42 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    const seedInput = screen.getByDisplayValue("42");
    expect(seedInput).toBeInTheDocument();
  });

  it("calls setClipSeed when seed input changes", async () => {
    const rpc = mockRpc();
    const onRefresh = vi.fn();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote()]}
        activeClip={{ clipId: 10, seed: 0 }}
        rpc={rpc}
        onRefresh={onRefresh}
      />,
    );
    const seedInput = document.querySelector(".nop-seed-input") as HTMLInputElement;
    fireEvent.change(seedInput, { target: { value: "99" } });
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.setClipSeed", { clipId: 10, seed: 99 });
    });
  });

  it("calls setNoteChance when chance slider changes (single note)", async () => {
    const rpc = mockRpc();
    const onRefresh = vi.fn();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote({ chance: 1 })]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={onRefresh}
      />,
    );
    const sliders = document.querySelectorAll(".nop-slider");
    const chanceSlider = sliders[0] as HTMLInputElement;
    fireEvent.change(chanceSlider, { target: { value: "0.5" } });
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.setNoteChance", { noteId: 1, chance: 0.5 });
    });
  });

  it("calls setNoteRepeatCount when input changes", async () => {
    const rpc = mockRpc();
    const onRefresh = vi.fn();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote({ repeatCount: 0 })]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={onRefresh}
      />,
    );
    const inputs = document.querySelectorAll(".nop-input");
    const repeatInput = inputs[0] as HTMLInputElement;
    fireEvent.change(repeatInput, { target: { value: "3" } });
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.setNoteRepeatCount", { noteId: 1, repeatCount: 3 });
    });
  });

  it("uses beginTransaction/endTransaction for multi-note bulk edit", async () => {
    const rpc = mockRpc();
    const onRefresh = vi.fn();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1, 2])}
        notes={[mkNote({ noteId: 1, noteGain: 1 }), mkNote({ noteId: 2, noteGain: 1 })]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={onRefresh}
      />,
    );
    const sliders = document.querySelectorAll(".nop-slider");
    // Slider order: 0=Chance, 1=RepeatCurve, 2=Gain, 3=Pan, 4=Pitch, 5=Timbre, 6=Pressure
    const gainSlider = sliders[2] as HTMLInputElement;
    fireEvent.change(gainSlider, { target: { value: "1.5" } });
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.beginTransaction", { name: "set gain" });
      expect(rpc.call).toHaveBeenCalledWith("project.setNoteGain", { noteId: 1, gain: 1.5 });
      expect(rpc.call).toHaveBeenCalledWith("project.setNoteGain", { noteId: 2, gain: 1.5 });
      expect(rpc.call).toHaveBeenCalledWith("project.endTransaction");
    });
  });

  it("shows '—' for mixed values when multiple notes differ", () => {
    const rpc = mockRpc();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1, 2])}
        notes={[
          mkNote({ noteId: 1, chance: 1, noteGain: 0.5 }),
          mkNote({ noteId: 2, chance: 0.5, noteGain: 0.5 }),
        ]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    const mixedEls = document.querySelectorAll(".nop-value--mixed");
    expect(mixedEls.length).toBeGreaterThan(0);
    expect(mixedEls[0].textContent).toBe("\u2014");
  });

  it("shows actual value when multiple notes share the same value", () => {
    const rpc = mockRpc();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1, 2])}
        notes={[
          mkNote({ noteId: 1, chance: 0.75 }),
          mkNote({ noteId: 2, chance: 0.75 }),
        ]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    expect(screen.getByText(/75%/)).toBeInTheDocument();
  });

  it("collapse toggle persists to localStorage", () => {
    const rpc = mockRpc();
    render(
      <NoteOperatorsPane
        selectedNoteIds={new Set([1])}
        notes={[mkNote()]}
        activeClip={{ clipId: 10 }}
        rpc={rpc}
        onRefresh={() => {}}
      />,
    );
    const header = document.querySelector(".nop-header")!;
    fireEvent.click(header);
    expect(localStorage.getItem("noteOperatorsCollapsed")).toBe("true");
    fireEvent.click(header);
    expect(localStorage.getItem("noteOperatorsCollapsed")).toBe("false");
  });
});
