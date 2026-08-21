import { describe, it, expect, afterEach } from "vitest";
import { render, screen, cleanup } from "@testing-library/react";
import FmAnalysisPanel from "./FmAnalysisPanel";
import { useAnalysisStore } from "../store/analysisStore";
import { useProjectStore } from "../store/projectStore";

function seedTrack(levelOverrides: number[] = [], voiceCount = 4, algo = 2) {
  const opEgLevel = Array.from({ length: 6 }, (_, i) => levelOverrides[i] ?? 0.5);
  useAnalysisStore.setState({
    tracks: [{ opEgLevel, activeVoices: voiceCount, algorithm: algo }],
  });
  useProjectStore.setState({
    snapshot: {
      tracks: [
        {
          index: 0,
          name: "Track 1",
          color: 0x5b9bd5,
          volume: 0.8,
          pan: 0,
          muted: false,
          soloed: false,
          armed: false,
          inputMonitor: false,
          height: 80,
          midiChannel: 0,
          clipCount: 0,
          trackType: 0,
          effectiveMuted: false,
          effectiveSoloed: false,
        },
      ],
    } as any,
  });
}

afterEach(() => {
  cleanup();
  useAnalysisStore.setState({ tracks: [] });
  useProjectStore.setState({ snapshot: null });
});

describe("FmAnalysisPanel", () => {
  it("renders without crashing", () => {
    render(<FmAnalysisPanel />);
    expect(document.querySelector(".fm-analysis")).toBeInTheDocument();
  });

  it("shows empty message when no data", () => {
    render(<FmAnalysisPanel />);
    expect(screen.getByText(/No active FM synth/)).toBeInTheDocument();
  });

  it("shows operator meters when data is present", () => {
    seedTrack();
    render(<FmAnalysisPanel />);
    expect(screen.getByText("Op 1")).toBeInTheDocument();
    expect(screen.getByText("Op 6")).toBeInTheDocument();
  });

  it("displays all 6 operator labels", () => {
    seedTrack();
    render(<FmAnalysisPanel />);
    for (let i = 1; i <= 6; i++) {
      expect(screen.getByText(`Op ${i}`)).toBeInTheDocument();
    }
  });

  it("displays the algorithm number", () => {
    seedTrack([], 4, 5);
    render(<FmAnalysisPanel />);
    expect(screen.getByText("Alg 6")).toBeInTheDocument();
  });

  it("displays active voice count", () => {
    seedTrack([], 3, 1);
    render(<FmAnalysisPanel />);
    expect(screen.getByText("3 voices")).toBeInTheDocument();
  });

  it("operator bars show percentage readouts", () => {
    seedTrack([0.73, 0.22, 1.0, 0.0, 0.5, 0.88]);
    render(<FmAnalysisPanel />);
    expect(screen.getByText("73%")).toBeInTheDocument();
    expect(screen.getByText("22%")).toBeInTheDocument();
    expect(screen.getByText("100%")).toBeInTheDocument();
    expect(screen.getByText("0%")).toBeInTheDocument();
    expect(screen.getByText("50%")).toBeInTheDocument();
    expect(screen.getByText("88%")).toBeInTheDocument();
  });

  it("hot-level operator gets correct CSS class", () => {
    seedTrack([0.80]);
    const { container } = render(<FmAnalysisPanel />);
    const fills = container.querySelectorAll(".fm-bar__fill--hot");
    expect(fills.length).toBe(1);
  });
});
