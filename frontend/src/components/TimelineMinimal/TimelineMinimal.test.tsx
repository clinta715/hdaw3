import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, cleanup } from "@testing-library/react";
import TimelineMinimal from "./TimelineMinimal";
import { rpc } from "../../rpc";
import { useProjectStore } from "../../store/projectStore";
import { useTransportStore } from "../../store/transportStore";
import { useUiStore } from "../../store/uiStore";
import { useTransportExtrasStore } from "../../store/transportExtrasStore";
import { useMarkerStore } from "../../store/markerStore";
import type { TrackSnapshot, ClipSnapshot } from "../../rpc/types";

vi.mock("../../rpc", () => ({ rpc: { call: vi.fn() } }));

vi.mock("../WaveformCanvas", () => ({
  WaveformCanvas: ({ width, height }: { width: number; height: number }) => (
    <div data-testid="waveform-canvas" style={{ width, height }} />
  ),
}));

vi.mock("../MidiThumbnailCanvas", () => ({
  MidiThumbnailCanvas: ({ width, height }: { width: number; height: number }) => (
    <div data-testid="midi-canvas" style={{ width, height }} />
  ),
}));

vi.mock("../TimelineContextMenu", () => ({
  TimelineContextMenu: () => <div data-testid="context-menu" />,
}));

vi.mock("../AddTrackMenu", () => ({
  AddTrackMenu: ({ label, triggerClassName }: { label?: string; triggerClassName?: string }) => (
    <div data-testid="add-track-menu" data-label={label} className={triggerClassName} />
  ),
}));

vi.mock("../PopUpBrowser", () => ({
  default: () => <div data-testid="popup-browser" />,
}));

vi.mock("../ArrangerLane", () => ({
  ArrangerLane: () => <div data-testid="arranger-lane" />,
}));

vi.mock("../../hooks/useTimelineDrag", () => ({
  useTimelineDrag: () => ({
    dragState: null,
    handleClipMouseDown: vi.fn(),
    dragSelectedIdsRef: { current: new Set() },
    dragCursor: undefined,
    dragPreviewStyle: undefined,
    dragPreviewClip: undefined,
    dragPreviewHeight: 0,
    paintTiles: [],
    paintCount: 0,
  }),
}));

vi.mock("../../hooks/useTimelineTrim", () => ({
  useTimelineTrim: () => ({
    handleTrimStart: vi.fn(),
    trimState: null,
  }),
}));

vi.mock("../../hooks/useTimelineFade", () => ({
  useTimelineFade: () => ({
    handleFadeStart: vi.fn(),
    fadeDrag: null,
  }),
}));

vi.mock("../../hooks/useTimelineLoopDrag", () => ({
  useTimelineLoopDrag: () => ({
    startLoopDrag: vi.fn(),
    dispLoopStart: 0,
    dispLoopEnd: 8,
  }),
}));

vi.mock("../../hooks/useTimelineRubberBand", () => ({
  useTimelineRubberBand: () => ({
    handleRubberBandStart: vi.fn(),
    rubberBand: null,
    rubberBandJustCompleted: { current: false },
  }),
}));

vi.mock("../../hooks/useTimelineZoom", () => ({
  useTimelineZoom: () => ({
    pps: 80,
    setPps: vi.fn(),
    zoomIn: vi.fn(),
    zoomOut: vi.fn(),
    zoomFit: vi.fn(),
    zoomToRange: vi.fn(),
  }),
}));

vi.mock("./useTimelineRuler", () => ({
  useTimelineRuler: () => ({
    isScrubbing: false,
    handleRulerMouseDown: vi.fn(),
    zoomRect: null,
  }),
}));

vi.mock("./useTimelineDrop", () => ({
  useTimelineDrop: () => ({
    handleDrop: vi.fn(),
  }),
}));

vi.mock("./useTimelineKeyboard", () => ({
  useTimelineKeyboard: vi.fn(),
}));

vi.mock("./useTimelineClipOps", () => ({
  useTimelineClipOps: () => ({
    contextMenu: null,
    setContextMenu: vi.fn(),
    emptyContextMenu: null,
    setEmptyContextMenu: vi.fn(),
    belowMenu: null,
    setBelowMenu: vi.fn(),
    browseClipTarget: null,
    handleContextMenu: vi.fn(),
    handleMarkerContextMenu: vi.fn(),
    handleCloseContextMenu: vi.fn(),
    handleBrowseClip: vi.fn(),
    handleBrowseSelect: vi.fn(),
    handleDeleteClip: vi.fn(),
    handleDuplicateClip: vi.fn(),
    handleSplitClip: vi.fn(),
    pasteClipboard: vi.fn(),
  }),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkTrack(index: number, over: Partial<TrackSnapshot> = {}): TrackSnapshot {
  return {
    index,
    name: `Track ${index}`,
    color: 0x5b9bd5,
    volume: 1,
    pan: 0,
    muted: false,
    soloed: false,
    armed: false,
    inputMonitor: false,
    height: 56,
    midiChannel: 0,
    clipCount: 0,
    trackType: 0,
    effectiveMuted: false,
    effectiveSoloed: false,
    ...over,
  };
}

function mkClip(clipId: number, trackIndex: number, over: Partial<ClipSnapshot> = {}): ClipSnapshot {
  return {
    clipId,
    trackIndex,
    name: `Clip ${clipId}`,
    sourceFile: "",
    startBeat: 0,
    durationBeats: 4,
    offset: 0,
    gain: 1,
    fadeIn: 0,
    fadeOut: 0,
    looping: false,
    muted: false,
    isMidi: false,
    sourceBpm: 120,
    stretchMode: 0,
    stretchRatio: 1,
    sourceDuration: 0,
    isGhost: false,
    ghostSourceId: -1,
    gainEnvelope: [],
    activeTake: 0,
    takeCount: 0,
    takes: [],
    ...over,
  };
}

describe("TimelineMinimal", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
    useUiStore.setState({
      selectedClipIds: new Set(),
      scrollX: 0,
      pixelsPerBeat: 80,
      lastSelectedClipId: null,
    });
    useTransportExtrasStore.setState({ followPlayhead: false });
    useMarkerStore.setState({ markers: [] });
  });

  afterEach(() => cleanup());

  it("renders without crashing", () => {
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".timeline-minimal")).toBeTruthy();
  });

  it("shows empty state when no tracks or clips", () => {
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".tl-empty-overlay")).toBeTruthy();
    expect(screen.getByText("No tracks yet")).toBeInTheDocument();
  });

  it("renders a ruler element", () => {
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".tl-ruler")).toBeTruthy();
  });

  it("renders track lanes when tracks exist", () => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: {
          bpm: 120,
          isPlaying: false,
          isLooping: false,
          isRecording: false,
          loopStart: 0,
          loopEnd: 8,
          currentTimeSeconds: 0,
          sampleRate: 44100,
          timeSigNumerator: 4,
          timeSigDenominator: 4,
        },
        tracks: [mkTrack(0), mkTrack(1)],
        clips: [],
        scaleRoot: 0,
        scaleMode: 0,
      },
    });
    const { container } = render(<TimelineMinimal />);
    const rows = container.querySelectorAll(".tl-track-row");
    expect(rows.length).toBe(2);
  });

  it("renders clip rectangles when clips exist", () => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: {
          bpm: 120,
          isPlaying: false,
          isLooping: false,
          isRecording: false,
          loopStart: 0,
          loopEnd: 8,
          currentTimeSeconds: 0,
          sampleRate: 44100,
          timeSigNumerator: 4,
          timeSigDenominator: 4,
        },
        tracks: [mkTrack(0)],
        clips: [mkClip(1, 0)],
        scaleRoot: 0,
        scaleMode: 0,
      },
    });
    const { container } = render(<TimelineMinimal />);
    const clipEl = container.querySelector('[data-clip-id="1"]');
    expect(clipEl).toBeTruthy();
    expect(clipEl?.classList.contains("tl-clip")).toBe(true);
  });

  it("zoom controls are present", () => {
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".tl-toolbar")).toBeTruthy();
    expect(screen.getByTitle("Zoom Out")).toBeInTheDocument();
    expect(screen.getByTitle("Zoom In")).toBeInTheDocument();
    expect(screen.getByTitle("Fit All")).toBeInTheDocument();
  });

  it("playhead element is present", () => {
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".tl-playhead")).toBeTruthy();
  });

  it("renders without crashing with multiple tracks and clips", () => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: {
          bpm: 120,
          isPlaying: false,
          isLooping: false,
          isRecording: false,
          loopStart: 0,
          loopEnd: 8,
          currentTimeSeconds: 0,
          sampleRate: 44100,
          timeSigNumerator: 4,
          timeSigDenominator: 4,
        },
        tracks: [mkTrack(0), mkTrack(1, { trackType: 1 }), mkTrack(2)],
        clips: [
          mkClip(1, 0, { startBeat: 0, durationBeats: 4 }),
          mkClip(2, 0, { startBeat: 5, durationBeats: 3 }),
          mkClip(3, 1, { startBeat: 2, durationBeats: 6, isMidi: true }),
        ],
        scaleRoot: 0,
        scaleMode: 0,
      },
    });
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".timeline-minimal")).toBeTruthy();
    const rows = container.querySelectorAll(".tl-track-row");
    expect(rows.length).toBe(3);
    expect(container.querySelector('[data-clip-id="1"]')).toBeTruthy();
    expect(container.querySelector('[data-clip-id="2"]')).toBeTruthy();
    expect(container.querySelector('[data-clip-id="3"]')).toBeTruthy();
  });

  it("toolbar renders", () => {
    const { container } = render(<TimelineMinimal />);
    const toolbar = container.querySelector(".tl-toolbar");
    expect(toolbar).toBeTruthy();
    const label = container.querySelector(".tl-tb-label");
    expect(label).toBeTruthy();
    expect(label?.textContent).toMatch(/px\/beat/);
  });

  it("handles empty project gracefully", () => {
    useProjectStore.setState({ snapshot: null });
    const { container } = render(<TimelineMinimal />);
    expect(container.querySelector(".timeline-minimal")).toBeTruthy();
    expect(container.querySelector(".tl-empty-overlay")).toBeTruthy();
  });
});
