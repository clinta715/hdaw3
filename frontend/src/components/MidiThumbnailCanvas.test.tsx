import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup, act } from "@testing-library/react";
import { MidiThumbnailCanvas } from "./MidiThumbnailCanvas";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import type { ClipSnapshot, NoteSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkClip(): ClipSnapshot {
  return {
    clipId: 7, trackIndex: 0, name: "MIDI", sourceFile: "",
    startBeat: 0, durationBeats: 4, offset: 0, gain: 1, fadeIn: 0, fadeOut: 0,
    looping: false, muted: false, isMidi: true, sourceBpm: 0, stretchMode: 0,
    stretchRatio: 1, sourceDuration: 0, isGhost: false, ghostSourceId: -1,
    gainEnvelope: [],
  };
}

describe("MidiThumbnailCanvas", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue([]);
  });

  afterEach(() => cleanup());

  it("renders a canvas and does not fetch when notes are already loaded", () => {
    const clip = mkClip();
    const notes: NoteSnapshot[] = [
      { noteId: 1, pitch: 60, velocity: 100, startBeat: 0, durationBeats: 1 },
      { noteId: 2, pitch: 64, velocity: 80, startBeat: 1, durationBeats: 1 },
    ];
    useProjectStore.setState({ notesByClip: new Map([[clip.clipId, notes]]) });

    const { container } = render(
      <MidiThumbnailCanvas clip={clip} width={120} height={48} color="#9b59b6" />
    );
    expect(container.querySelector("canvas")).not.toBeNull();
    expect(mockedCall).not.toHaveBeenCalled();
  });

  it("renders with trimOverride without crashing", () => {
    const clip = mkClip();
    const { container } = render(
      <MidiThumbnailCanvas
        clip={clip}
        width={200}
        height={40}
        trimOverride={{ offset: 0, durationBeats: 2 }}
      />
    );
    expect(container.querySelector("canvas")).toBeTruthy();
  });

  it("lazy-fetches notes once when they are absent", async () => {
    const clip = mkClip();
    useProjectStore.setState({ notesByClip: new Map() });
    await act(async () => {
      render(<MidiThumbnailCanvas clip={clip} width={120} height={48} />);
    });
    expect(mockedCall).toHaveBeenCalled();
  });
});
