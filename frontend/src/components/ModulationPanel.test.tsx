import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import ModulationPanel from "./ModulationPanel";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";

vi.mock("../rpc", () => ({
  rpc: {
    call: vi.fn().mockResolvedValue([]),
  },
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

function mkTrack(index: number, name: string) {
  return {
    index, name, color: 0x5b9bd5, volume: 1, pan: 0,
    muted: false, soloed: false, armed: false, inputMonitor: false,
    height: 80, midiChannel: 0, clipCount: 0, trackType: 0,
    effectiveMuted: false, effectiveSoloed: false,
  };
}

const ONE_LFO = [
  {
    index: 0, name: "LFO 1", waveform: 0, rate: 1.0, rateSync: 0,
    depth: 0.5, bipolar: false, phaseOffset: 0, targetParamID: 1, enabled: true,
  },
];

const TWO_LFOS = [
  ...ONE_LFO,
  {
    index: 1, name: "Tremolo", waveform: 2, rate: 4.0, rateSync: 0,
    depth: 0.75, bipolar: true, phaseOffset: 90, targetParamID: 2, enabled: false,
  },
];

describe("ModulationPanel", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue([]);
    useUiStore.setState({ selectedTrackIndex: 0 });
    useProjectStore.setState({
      snapshot: {
        tracks: [mkTrack(0, "Track 1"), mkTrack(1, "Synth")],
        clips: [], tempo: [], markers: [],
      } as any,
    });
  });

  afterEach(() => { cleanup(); });

  describe("empty state", () => {
    it("shows no-track message when snapshot is null", async () => {
      useProjectStore.setState({ snapshot: null });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText(/No track selected/)).toBeInTheDocument();
    });

    it("shows no-LFO message when LFO list is empty", async () => {
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText(/No LFOs/)).toBeInTheDocument();
      expect(screen.getByText("+ Add LFO")).toBeInTheDocument();
    });

    it("renders the header with track name", async () => {
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText(/Track 1/)).toBeInTheDocument();
    });
  });

  describe("LFO display", () => {
    it("renders LFO cards when LFOs exist", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(TWO_LFOS);
        return Promise.resolve([]);
      });
      const { container } = render(<ModulationPanel />);
      await flushRead();
      expect(container.querySelectorAll(".mod-lfo-card").length).toBe(2);
    });

    it("does not show no-LFO message when LFOs exist", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.queryByText(/No LFOs/)).not.toBeInTheDocument();
    });

    it("shows LFO name", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("LFO 1")).toBeInTheDocument();
    });

    it("shows custom LFO name when set", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(TWO_LFOS);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("Tremolo")).toBeInTheDocument();
    });

    it("shows LFO rate formatted to 1 decimal", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("1.0 Hz")).toBeInTheDocument();
    });

    it("shows LFO depth as percentage", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("50%")).toBeInTheDocument();
    });

    it("shows phase offset in degrees", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(TWO_LFOS);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("90\u00B0")).toBeInTheDocument();
    });
  });

  describe("target selection", () => {
    it("renders target dropdown with built-in options", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      const selects = screen.getAllByRole("combobox");
      const targetSelect = selects[0];
      const options = targetSelect.querySelectorAll("option");
      expect(options.length).toBeGreaterThanOrEqual(3);
    });

    it("includes Volume, Pan, Mute as built-in targets", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("Volume")).toBeInTheDocument();
      expect(screen.getByText("Pan")).toBeInTheDocument();
      expect(screen.getByText("Mute")).toBeInTheDocument();
    });

    it("includes FX params in target list when available", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([
          { slotIndex: 0, paramIndex: 10, name: "Reverb Mix", automatable: true },
        ]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("Reverb Mix")).toBeInTheDocument();
    });
  });

  describe("waveform selector", () => {
    it("renders all waveform options", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      expect(screen.getByText("Sine")).toBeInTheDocument();
      expect(screen.getByText("Triangle")).toBeInTheDocument();
      expect(screen.getByText("Saw")).toBeInTheDocument();
      expect(screen.getByText("Square")).toBeInTheDocument();
      expect(screen.getByText("Random")).toBeInTheDocument();
    });
  });

  describe("rate slider", () => {
    it("renders rate slider with correct attributes", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const { container } = render(<ModulationPanel />);
      await flushRead();
      const rateSlider = container.querySelector('input[type="range"][min="0.1"][max="20"]') as HTMLInputElement;
      expect(rateSlider).toBeInTheDocument();
      expect(rateSlider.value).toBe("1");
    });
  });

  describe("depth slider", () => {
    it("renders depth slider with correct attributes", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const { container } = render(<ModulationPanel />);
      await flushRead();
      const depthSlider = container.querySelector('input[type="range"][min="0"][max="1"]') as HTMLInputElement;
      expect(depthSlider).toBeInTheDocument();
      expect(depthSlider.value).toBe("0.5");
    });
  });

  describe("add LFO", () => {
    it("clicking Add LFO calls project.addLfo RPC", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<ModulationPanel />);
      await flushRead();
      await user.click(screen.getByText("+ Add LFO"));
      expect(mockedCall).toHaveBeenCalledWith("project.addLfo", { trackIndex: 0 });
    });
  });

  describe("remove LFO", () => {
    it("clicking remove button calls project.removeLfo RPC", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<ModulationPanel />);
      await flushRead();
      await user.click(screen.getByText("\u00D7"));
      expect(mockedCall).toHaveBeenCalledWith("project.removeLfo", {
        trackIndex: 0, lfoIndex: 0,
      });
    });
  });

  describe("bipolar and enabled checkboxes", () => {
    it("renders bipolar checkbox unchecked when bipolar=false", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      const checkboxes = screen.getAllByRole("checkbox");
      const bipolarCb = checkboxes.find((cb) => cb.closest("label")?.textContent?.includes("Bipolar"));
      expect(bipolarCb).toBeDefined();
      expect(bipolarCb).not.toBeChecked();
    });

    it("renders enabled checkbox checked when enabled=true", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      render(<ModulationPanel />);
      await flushRead();
      const checkboxes = screen.getAllByRole("checkbox");
      const enabledCb = checkboxes.find((cb) => cb.closest("label")?.textContent?.includes("Enabled"));
      expect(enabledCb).toBeDefined();
      expect(enabledCb).toBeChecked();
    });

    it("toggling bipolar calls setLfoParam with optimistic update", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<ModulationPanel />);
      await flushRead();
      const checkboxes = screen.getAllByRole("checkbox");
      const bipolarCb = checkboxes.find((cb) => cb.closest("label")?.textContent?.includes("Bipolar"))!;
      await user.click(bipolarCb);
      expect(mockedCall).toHaveBeenCalledWith("project.setLfoParam", expect.objectContaining({
        trackIndex: 0, lfoIndex: 0, paramName: "bipolar", value: 1,
      }));
    });
  });

  describe("rpc fetch on mount", () => {
    it("fetches LFOs and targets on mount", async () => {
      render(<ModulationPanel />);
      await flushRead();
      expect(mockedCall).toHaveBeenCalledWith("read.getModulationLfos", { trackIndex: 0 });
      expect(mockedCall).toHaveBeenCalledWith("read.getAutomatableParams", { trackIndex: 0 });
    });
  });

  describe("rate change", () => {
    it("changing rate slider calls setLfoParam", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const { container } = render(<ModulationPanel />);
      await flushRead();
      const rateSlider = container.querySelector('input[type="range"][min="0.1"][max="20"]') as HTMLInputElement;
      fireEvent.change(rateSlider, { target: { value: "5" } });
      expect(mockedCall).toHaveBeenCalledWith("project.setLfoParam", expect.objectContaining({
        trackIndex: 0, lfoIndex: 0, paramName: "rate",
      }));
    });
  });

  describe("depth change", () => {
    it("changing depth slider calls setLfoParam", async () => {
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getModulationLfos") return Promise.resolve(ONE_LFO);
        if (method === "read.getAutomatableParams") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const { container } = render(<ModulationPanel />);
      await flushRead();
      const depthSlider = container.querySelector('input[type="range"][min="0"][max="1"]') as HTMLInputElement;
      fireEvent.change(depthSlider, { target: { value: "0.8" } });
      expect(mockedCall).toHaveBeenCalledWith("project.setLfoParam", expect.objectContaining({
        trackIndex: 0, lfoIndex: 0, paramName: "depth",
      }));
    });
  });
});
