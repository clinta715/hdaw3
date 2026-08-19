import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import MixerStrip from "./MixerStrip";
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

const meter: MeterLevels = { l: 0.5, r: 0.4 };

describe("MixerStrip", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
  });

  afterEach(() => cleanup());

  it("renders the track name", () => {
    render(<MixerStrip track={mkTrack()} meter={meter} />);
    expect(screen.getByText("Synth")).toBeInTheDocument();
  });

  it("shows a live volume readout as a percentage", () => {
    render(<MixerStrip track={mkTrack({ volume: 0.8 })} meter={meter} />);
    expect(screen.getByText("80%")).toBeInTheDocument();
  });

  it("updates the readout while the fader moves", () => {
    const { container } = render(<MixerStrip track={mkTrack({ volume: 0.8 })} meter={meter} />);
    const fader = container.querySelector(".ms-fader") as HTMLInputElement;
    fireEvent.change(fader, { target: { value: "0.5" } });
    expect(screen.getByText("50%")).toBeInTheDocument();
  });

  it("exposes a 0..1 fader range", () => {
    const { container } = render(<MixerStrip track={mkTrack()} meter={meter} />);
    const fader = container.querySelector(".ms-fader") as HTMLInputElement;
    expect(fader.min).toBe("0");
    expect(fader.max).toBe("1");
  });

  it("renders M/S/R buttons for a normal track", () => {
    render(<MixerStrip track={mkTrack()} meter={meter} />);
    expect(screen.getByText("M")).toBeInTheDocument();
    expect(screen.getByText("S")).toBeInTheDocument();
    expect(screen.getByText("R")).toBeInTheDocument();
  });

  it("hides M/S/R buttons on the master strip", () => {
    render(<MixerStrip track={mkTrack({ index: -1, name: "Master" })} meter={meter} isMaster />);
    expect(screen.queryByText("M")).toBeNull();
    expect(screen.queryByText("S")).toBeNull();
    expect(screen.queryByText("R")).toBeNull();
  });

  it("hides the pan fader on the master strip", () => {
    const { container } = render(
      <MixerStrip track={mkTrack({ index: -1, name: "Master" })} meter={meter} isMaster />
    );
    expect(container.querySelector(".ms-pan-fader")).toBeNull();
  });

  it("master strip fader commit calls project.setMasterGain", () => {
    const { container } = render(
      <MixerStrip track={mkTrack({ index: -1, name: "Master", volume: 1 })} meter={meter} isMaster />
    );
    const fader = container.querySelector(".ms-fader") as HTMLInputElement;
    fireEvent.change(fader, { target: { value: "0.5" } });
    fireEvent.mouseUp(fader);
    expect(mockedCall).toHaveBeenCalledWith("project.setMasterGain", { gain: 0.5 });
    expect(mockedCall).not.toHaveBeenCalledWith(
      "project.setTrackVolume",
      expect.anything()
    );
  });

  it("clicking M calls project.setTrackMuted with the toggled value", () => {
    render(<MixerStrip track={mkTrack({ index: 2, muted: false })} meter={meter} />);
    fireEvent.click(screen.getByText("M"));
    expect(mockedCall).toHaveBeenCalledWith("project.setTrackMuted", { trackIndex: 2, muted: true });
  });

  it("clicking S calls project.setTrackSoloed with the toggled value", () => {
    render(<MixerStrip track={mkTrack({ index: 2, soloed: true })} meter={meter} />);
    fireEvent.click(screen.getByText("S"));
    expect(mockedCall).toHaveBeenCalledWith("project.setTrackSoloed", { trackIndex: 2, soloed: false });
  });
});
