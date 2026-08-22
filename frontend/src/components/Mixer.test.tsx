import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import Mixer from "./Mixer";
import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";
import { rpc } from "../rpc";
import type { TrackSnapshot, MeterLevels } from "../rpc/types";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function mkTrack(over: Partial<TrackSnapshot> = {}): TrackSnapshot {
  return {
    index: 1, name: "Synth", color: 0x5b9bd5, volume: 0.8, pan: 0,
    muted: false, soloed: false, armed: false, inputMonitor: false,
    height: 80, midiChannel: 0, clipCount: 0, trackType: 0,
    effectiveMuted: false, effectiveSoloed: false, ...over,
  };
}

describe("Mixer", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
    useProjectStore.setState({ snapshot: null });
    useMeterStore.setState({ master: { l: 0, r: 0, rmsL: 0, rmsR: 0, lufs: 0 }, tracks: [] });
  });

  afterEach(() => cleanup());

  it("renders 'No tracks' when track list is empty", () => {
    render(<Mixer />);
    expect(screen.getByText("No tracks")).toBeInTheDocument();
  });

  it("renders '+ Add Track' button in empty state", () => {
    render(<Mixer />);
    expect(screen.getByText("+ Add Track")).toBeInTheDocument();
  });

  it("clicking '+ Add Track' calls rpc.call('project.addTrack')", () => {
    render(<Mixer />);
    fireEvent.click(screen.getByText("+ Add Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.addTrack");
  });

  it("renders a MixerStrip for each track in the snapshot", () => {
    const tracks = [mkTrack({ index: 0, name: "Drums" }), mkTrack({ index: 1, name: "Bass" })];
    useProjectStore.setState({ snapshot: { tracks } as any });
    const { container } = render(<Mixer />);
    const strips = container.querySelectorAll(".mixer-strip");
    expect(strips.length).toBe(3);
  });

  it("renders a master strip with name 'Master'", () => {
    render(<Mixer />);
    expect(screen.getByText("Master")).toBeInTheDocument();
  });

  it("renders correct number of strip elements (tracks + master)", () => {
    const tracks = [mkTrack({ index: 0 }), mkTrack({ index: 1 }), mkTrack({ index: 2 })];
    useProjectStore.setState({ snapshot: { tracks } as any });
    const { container } = render(<Mixer />);
    const strips = container.querySelectorAll(".mixer-strip");
    expect(strips.length).toBe(4);
  });

  it("applies mixer-strip--master class to master strip", () => {
    const { container } = render(<Mixer />);
    const masterDiv = container.querySelector(".mixer-master");
    expect(masterDiv).not.toBeNull();
    const masterStrip = masterDiv!.querySelector(".mixer-strip--master");
    expect(masterStrip).not.toBeNull();
  });

  it("passes meter data from meter store to strips", () => {
    const tracks = [mkTrack({ index: 0 })];
    const masterMeter: MeterLevels = { l: 0.7, r: 0.6, rmsL: 0, rmsR: 0, lufs: 0 };
    const trackMeters: MeterLevels[] = [{ l: 0.5, r: 0.4, rmsL: 0, rmsR: 0, lufs: 0 }];
    useProjectStore.setState({ snapshot: { tracks } as any });
    useMeterStore.setState({ master: masterMeter, tracks: trackMeters });
    const { container } = render(<Mixer />);
    const meters = container.querySelectorAll(".ms-meter");
    expect(meters.length).toBeGreaterThanOrEqual(2);
  });

  it("renders without crashing with empty track list", () => {
    expect(() => render(<Mixer />)).not.toThrow();
  });

  it("renders without crashing with multiple tracks", () => {
    const tracks = [mkTrack({ index: 0 }), mkTrack({ index: 1 }), mkTrack({ index: 2 })];
    useProjectStore.setState({ snapshot: { tracks } as any });
    expect(() => render(<Mixer />)).not.toThrow();
  });
});
