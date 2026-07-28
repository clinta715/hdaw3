import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import { TimelineContextMenu } from "./TimelineContextMenu";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import type { ClipSnapshot, TransportSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

const transport: TransportSnapshot = {
  bpm: 120, isPlaying: false, isLooping: false, isRecording: false,
  loopStart: 0, loopEnd: 8, currentTimeSeconds: 0, sampleRate: 44100,
};

function mkClip(clipId: number): ClipSnapshot {
  return {
    clipId, trackIndex: 0, name: `Clip ${clipId}`, sourceFile: "",
    startBeat: 0, durationBeats: 4, offset: 0, gain: 1, fadeIn: 0, fadeOut: 0,
    looping: false, muted: false, isMidi: true, sourceBpm: 0, stretchMode: 0,
    stretchRatio: 1, sourceDuration: 0, isGhost: false, ghostSourceId: -1,
    gainEnvelope: [],
  };
}

function renderMenu(overrides: Partial<React.ComponentProps<typeof TimelineContextMenu>> = {}) {
  const props = {
    contextMenu: null,
    emptyContextMenu: null,
    belowMenu: null,
    clips: [],
    markers: [],
    selectedClipIds: new Set<number>(),
    transport,
    onClose: vi.fn(),
    onDeleteClip: vi.fn(),
    onDuplicateClip: vi.fn(),
    onSplitClip: vi.fn(),
    ...overrides,
  };
  render(<TimelineContextMenu {...props} />);
  return props;
}

describe("TimelineContextMenu", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
    useUiStore.setState({ clipClipboard: [], selectedClipIds: new Set() });
  });

  afterEach(() => cleanup());

  it("renders nothing when no menu is open", () => {
    const { container } = render(<TimelineContextMenu
      contextMenu={null} emptyContextMenu={null} belowMenu={null}
      clips={[]} markers={[]} selectedClipIds={new Set()} transport={transport}
      onClose={vi.fn()} onDeleteClip={vi.fn()} onDuplicateClip={vi.fn()} onSplitClip={vi.fn()}
    />);
    expect(container.querySelector(".clip-context-menu")).toBeNull();
  });

  describe("empty-lane menu", () => {
    it("offers Add Track, Add MIDI Clip, and Delete Track", () => {
      renderMenu({ emptyContextMenu: { x: 10, y: 10, beat: 0, trackIndex: 2 } });
      expect(screen.getByText("Add Track")).toBeInTheDocument();
      expect(screen.getByText("Add MIDI Clip")).toBeInTheDocument();
      expect(screen.getByText("Delete Track")).toBeInTheDocument();
    });

    it("Add Track calls project.addTrack and closes", () => {
      const { onClose } = renderMenu({ emptyContextMenu: { x: 10, y: 10, beat: 0, trackIndex: 0 } });
      fireEvent.mouseDown(screen.getByText("Add Track"));
      expect(mockedCall).toHaveBeenCalledWith("project.addTrack");
      expect(onClose).toHaveBeenCalled();
    });

    it("Delete Track calls project.removeTrack with the menu's track index", () => {
      const { onClose } = renderMenu({ emptyContextMenu: { x: 10, y: 10, beat: 0, trackIndex: 3 } });
      fireEvent.mouseDown(screen.getByText("Delete Track"));
      expect(mockedCall).toHaveBeenCalledWith("project.removeTrack", { trackIndex: 3 });
      expect(onClose).toHaveBeenCalled();
    });
  });

  describe("below-tracks menu", () => {
    it("offers Add Track and creates a track", () => {
      const { onClose } = renderMenu({ belowMenu: { x: 10, y: 10 } });
      fireEvent.mouseDown(screen.getByText("Add Track"));
      expect(mockedCall).toHaveBeenCalledWith("project.addTrack");
      expect(onClose).toHaveBeenCalled();
    });
  });

  describe("clip menu", () => {
    it("shows Delete and Duplicate; Delete invokes onDeleteClip", () => {
      const { onDeleteClip } = renderMenu({
        contextMenu: { x: 10, y: 10, type: "clip", clip: mkClip(1) },
        clips: [mkClip(1)],
      });
      fireEvent.mouseDown(screen.getByText("Delete"));
      expect(onDeleteClip).toHaveBeenCalled();
    });

    it("Duplicate invokes onDuplicateClip", () => {
      const { onDuplicateClip } = renderMenu({
        contextMenu: { x: 10, y: 10, type: "clip", clip: mkClip(1) },
        clips: [mkClip(1)],
      });
      fireEvent.mouseDown(screen.getByText("Duplicate"));
      expect(onDuplicateClip).toHaveBeenCalled();
    });
  });

  describe("marker menu", () => {
    it("Delete Marker calls project.removeMarker with the marker index", () => {
      renderMenu({
        contextMenu: { x: 10, y: 10, type: "marker", markerIndex: 4 },
        markers: [{ index: 4, name: "M", time: 2, color: 0 }],
      });
      fireEvent.mouseDown(screen.getByText("Delete Marker"));
      expect(mockedCall).toHaveBeenCalledWith("project.removeMarker", { index: 4 });
    });
  });
});
