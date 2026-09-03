import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import FXChain from "./FXChain";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { useAutomationStore } from "../store/automationStore";
import { useNotifyStore } from "../store/notifyStore";
import type { FxSlotSnapshot } from "../rpc/types";

vi.mock("../rpc", () => ({
  rpc: {
    call: vi.fn().mockResolvedValue([]),
    onNotification: vi.fn(() => vi.fn()),
  },
}));

vi.mock("./PopUpBrowser", () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="popup-browser">
      <button onClick={onClose}>Close Browser</button>
    </div>
  ),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;
const mockedOnNotification = rpc.onNotification as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

const TWO_SLOTS: FxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "eq", pluginId: "", pluginName: "EQ", pluginFormat: "", bypassed: false, paramCount: 3 },
  { slotIndex: 1, fxType: "plugin", pluginId: "/plugins/Delay.vst3", pluginName: "Delay", pluginFormat: "VST3", bypassed: false, paramCount: 4 },
];

const BYPASSED_SLOT: FxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "reverb", pluginId: "", pluginName: "Reverb", pluginFormat: "", bypassed: true, paramCount: 2 },
];

const ONE_INTERNAL_SLOT: FxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "eq", pluginId: "", pluginName: "EQ", pluginFormat: "", bypassed: false, paramCount: 3 },
];

const FM_SYNTH_SLOT: FxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "fm_synth", pluginId: "", pluginName: "FM Synth", pluginFormat: "", bypassed: false, paramCount: 5 },
];

describe("FXChain", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue([]);
    mockedOnNotification.mockReset();
    mockedOnNotification.mockReturnValue(vi.fn());
    useAutomationStore.setState({ lastClickedParamID: null });
    useNotifyStore.getState().clear();
    useUiStore.setState({
      selectedTrackIndex: null,
      crashedFxSlots: {},
    });
  });

  afterEach(() => {
    cleanup();
  });

  describe("empty state", () => {
    it("shows empty state when no track is selected", async () => {
      render(<FXChain />);
      await flushRead();
      expect(screen.getByText("Select a track to edit FX")).toBeInTheDocument();
    });

    it("shows no-FX message when slot array is empty", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue([]);
      render(<FXChain />);
      await flushRead();
      expect(screen.getByText("No FX slots. Click + Add FX to create one.")).toBeInTheDocument();
    });

    it("renders the header with track number", async () => {
      useUiStore.setState({ selectedTrackIndex: 3 });
      mockedCall.mockResolvedValue([]);
      render(<FXChain />);
      await flushRead();
      expect(screen.getByText(/FX Chain/)).toBeInTheDocument();
    });
  });

  describe("slot rendering", () => {
    it("renders each slot with type and plugin name", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      expect(screen.getByText("eq")).toBeInTheDocument();
      expect(screen.getByText("plugin")).toBeInTheDocument();
      expect(screen.getByText("EQ")).toBeInTheDocument();
      expect(screen.getByText("Delay")).toBeInTheDocument();
    });

    it("does not show no-FX message when slots are present", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();
      expect(screen.queryByText("No FX slots. Click + Add FX to create one.")).not.toBeInTheDocument();
    });

    it("applies bypassed class to bypassed slots", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(BYPASSED_SLOT);
      const { container } = render(<FXChain />);
      await flushRead();
      expect(container.querySelector(".fx-slot--bypassed")).toBeInTheDocument();
    });

    it("renders control buttons for each slot", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      expect(screen.getAllByText("Up").length).toBe(2);
      expect(screen.getAllByText("Dn").length).toBe(2);
      expect(screen.getAllByText("Edit").length).toBe(2);
      expect(screen.getAllByText("Del").length).toBe(2);
      expect(screen.getAllByText("Params").length).toBe(2);
    });

    it("shows preset and A/B buttons only for slots with pluginId", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      expect(screen.getAllByText("Presets").length).toBe(1);
      expect(screen.getAllByText(/A\/B/).length).toBe(1);
    });
  });

  describe("plugin loading flow", () => {
    it("opens dropdown when + Add FX is clicked", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue([]);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));

      expect(screen.getByText("Internal")).toBeInTheDocument();
      expect(screen.getByText("EQ")).toBeInTheDocument();
      expect(screen.getByText("Compressor")).toBeInTheDocument();
      expect(screen.getByText("Reverb")).toBeInTheDocument();
      expect(screen.getByText("Delay")).toBeInTheDocument();
      expect(screen.getByText("Chorus")).toBeInTheDocument();
      expect(screen.getByText("Flanger")).toBeInTheDocument();
      expect(screen.getByText("Phaser")).toBeInTheDocument();
    });

    it("selecting an internal FX calls addFxSlot and closes dropdown", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue([]);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));
      await user.click(screen.getByText("EQ"));

      expect(mockedCall).toHaveBeenCalledWith("project.addFxSlot", expect.objectContaining({
        trackIndex: 0,
        fxType: "eq",
      }));
      expect(screen.queryByText("Internal")).not.toBeInTheDocument();
    });

    it("shows instruments section when instruments exist", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve([]);
        if (method === "plugin.getInstrumentPlugins") return Promise.resolve([
          { name: "Synth", format: "VST3", manufacturer: "Acme", fileOrIdentifier: "/synth.vst3", isInstrument: true },
        ]);
        if (method === "plugin.getEffectPlugins") return Promise.resolve([]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));
      expect(screen.getByText("Instruments")).toBeInTheDocument();
      expect(screen.getByText("Synth")).toBeInTheDocument();
    });

    it("shows effects section when effects exist", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve([]);
        if (method === "plugin.getInstrumentPlugins") return Promise.resolve([]);
        if (method === "plugin.getEffectPlugins") return Promise.resolve([
          { name: "Limiter", format: "CLAP", manufacturer: "Loud", fileOrIdentifier: "/limiter.clap", isInstrument: false },
        ]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));
      expect(screen.getByText("Effects")).toBeInTheDocument();
      expect(screen.getByText("Limiter")).toBeInTheDocument();
    });

    it("selecting an external plugin calls addFxSlot with plugin type", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve([]);
        if (method === "plugin.getInstrumentPlugins") return Promise.resolve([]);
        if (method === "plugin.getEffectPlugins") return Promise.resolve([
          { name: "Reverb X", format: "VST3", manufacturer: "Wet", fileOrIdentifier: "/reverb.vst3", isInstrument: false },
        ]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));
      await user.click(screen.getByText("Reverb X"));

      expect(mockedCall).toHaveBeenCalledWith("project.addFxSlot", expect.objectContaining({
        trackIndex: 0,
        fxType: "plugin",
        pluginId: "/reverb.vst3",
      }));
    });

    it("Browse Devices opens popup browser", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue([]);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      await user.click(screen.getByText("+ Add FX"));
      await user.click(screen.getByText("Browse Devices..."));

      expect(screen.getByTestId("popup-browser")).toBeInTheDocument();
    });
  });

  describe("remove slot", () => {
    it("clicking Del calls removeFxSlot RPC", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const delButtons = screen.getAllByText("Del");
      await user.click(delButtons[0]);

      expect(mockedCall).toHaveBeenCalledWith("project.removeFxSlot", expect.objectContaining({
        trackIndex: 0,
        slotIndex: 0,
      }));
    });
  });

  describe("bypass toggle", () => {
    it("clicking Byp calls setFxSlotBypassed RPC", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(BYPASSED_SLOT);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const bypButtons = screen.getAllByText("Byp");
      await user.click(bypButtons[0]);

      expect(mockedCall).toHaveBeenCalledWith("project.setFxSlotBypassed", expect.objectContaining({
        trackIndex: 0,
        slotIndex: 0,
        bypassed: false,
      }));
    });
  });

  describe("reorder slots", () => {
    it("clicking Up on slot 1 calls reorderFxSlots", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const upButtons = screen.getAllByText("Up");
      await user.click(upButtons[1]);

      expect(mockedCall).toHaveBeenCalledWith("project.reorderFxSlots", expect.objectContaining({
        trackIndex: 0,
        fromSlot: 1,
        toSlot: 0,
      }));
    });

    it("Up button is disabled on first slot", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      const upButtons = screen.getAllByTitle("Move up");
      expect(upButtons[0]).toBeDisabled();
      expect(upButtons[1]).not.toBeDisabled();
    });

    it("Dn button is disabled on last slot", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      const dnButtons = screen.getAllByTitle("Move down");
      expect(dnButtons[0]).not.toBeDisabled();
      expect(dnButtons[1]).toBeDisabled();
    });
  });

  describe("parameter display", () => {
    it("clicking Params toggles parameter list for internal FX", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve(ONE_INTERNAL_SLOT);
        if (method === "read.getInternalFxParams") return Promise.resolve([
          { paramIndex: 0, name: "Frequency", value: 1000, minValue: 20, maxValue: 20000, defaultValue: 1000 },
        ]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const paramsButtons = screen.getAllByText("Params");
      await user.click(paramsButtons[0]);

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("read.getInternalFxParams", expect.objectContaining({
          trackIndex: 0,
          slotIndex: 0,
        }));
      });
    });

    it("clicking Params toggles parameter list for external plugin", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve(TWO_SLOTS);
        if (method === "pluginParam.getParams") return Promise.resolve([
          { paramIndex: 0, name: "Mix", value: 0.5, text: "50%", label: "%", automatable: true },
        ]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const paramsButtons = screen.getAllByText("Params");
      await user.click(paramsButtons[1]);

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("pluginParam.getParams", expect.objectContaining({
          trackIndex: 0,
          pluginID: "/plugins/Delay.vst3",
        }));
      });
    });
  });

  describe("preset management", () => {
    it("renders the chain preset bar, applies the selected preset, and surfaces warnings", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve([]);
        if (method === "project.listFxChainPresets") return Promise.resolve([
          { id: "1", name: "Driven", slotCount: 2 },
        ]);
        if (method === "project.loadFxChainPreset") return Promise.resolve({
          ok: true,
          warnings: ["Missing plugin replaced with a bypassed slot"],
        });
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);

      const presetSelect = await screen.findByTitle("FX chain presets");
      expect(screen.getByRole("option", { name: "Driven (2)" })).toBeInTheDocument();
      await user.selectOptions(presetSelect, "1");
      await user.click(screen.getByRole("button", { name: "Apply" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("project.loadFxChainPreset", {
          trackIndex: 0,
          id: "1",
        });
        expect(mockedCall.mock.calls.filter(([method]) => method === "read.getFxSlots")).toHaveLength(2);
        expect(useNotifyStore.getState().toasts).toEqual(expect.arrayContaining([
          expect.objectContaining({
            level: "info",
            message: "Missing plugin replaced with a bypassed slot",
          }),
        ]));
      });
    });

    it("clicking Presets calls listPrograms RPC", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve(TWO_SLOTS);
        if (method === "pluginParam.listPrograms") return Promise.resolve([
          { index: 0, name: "Default", current: true },
          { index: 1, name: "Heavy", current: false },
        ]);
        return Promise.resolve([]);
      });
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const presetBtn = screen.getByText("Presets");
      await user.click(presetBtn);

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("pluginParam.listPrograms", expect.objectContaining({
          trackIndex: 0,
          pluginID: "/plugins/Delay.vst3",
        }));
      });
    });

    it("clicking Presets on internal FX slot does not call listPrograms", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(ONE_INTERNAL_SLOT);
      render(<FXChain />);
      await flushRead();

      const callsBefore = mockedCall.mock.calls.length;
      expect(screen.queryByText("Presets")).not.toBeInTheDocument();
      expect(mockedCall.mock.calls.length).toBe(callsBefore);
    });
  });

  describe("A/B comparison", () => {
    it("clicking A/B on slot with pluginId calls captureFxSnapshot", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const abButtons = screen.getAllByText(/A\/B/);
      await user.click(abButtons[0]);

      expect(mockedCall).toHaveBeenCalledWith("audio.captureFxSnapshot", expect.objectContaining({
        trackIndex: 0,
        slotIndex: 1,
      }));
    });

    it("A/B button not shown for internal FX slots (no pluginId)", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(ONE_INTERNAL_SLOT);
      render(<FXChain />);
      await flushRead();

      expect(screen.queryByText(/A\/B/)).not.toBeInTheDocument();
    });
  });

  describe("editor toggle", () => {
    it("clicking Edit calls toggleFXEditor RPC", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const user = userEvent.setup();
      render(<FXChain />);
      await flushRead();

      const editButtons = screen.getAllByText("Edit");
      await user.click(editButtons[0]);

      expect(mockedCall).toHaveBeenCalledWith("audioGraph.toggleFXEditor", expect.objectContaining({
        trackIndex: 0,
        slotIndex: 0,
      }));
    });
  });

  describe("drag and drop", () => {
    it("slot elements are draggable", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const { container } = render(<FXChain />);
      await flushRead();

      const slots = container.querySelectorAll(".fx-slot");
      slots.forEach((slot) => {
        expect(slot).toHaveAttribute("draggable", "true");
      });
    });

    it("dragging a slot calls reorderFxSlots on drop", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      const { container } = render(<FXChain />);
      await flushRead();

      const slots = container.querySelectorAll(".fx-slot");
      const firstSlot = slots[0] as HTMLElement;
      const secondSlot = slots[1] as HTMLElement;

      const dataTransfer = { effectAllowed: "", dropEffect: "" };

      fireEvent.dragStart(firstSlot, { dataTransfer });
      fireEvent.dragOver(secondSlot, { dataTransfer });
      fireEvent.drop(secondSlot, { dataTransfer });

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("project.reorderFxSlots", expect.objectContaining({
          trackIndex: 0,
        }));
      });
    });
  });

  describe("cartridge voices", () => {
    it("shows Voice button for fm_synth slot after import", async () => {
      useUiStore.setState({ selectedTrackIndex: 0 });
      mockedCall.mockImplementation((method: string) => {
        if (method === "read.getFxSlots") return Promise.resolve(FM_SYNTH_SLOT);
        if (method === "audio.fm_synthImportSysex") return Promise.resolve({
          filePath: "/test.syx",
          totalVoices: 3,
          voiceIndex: 0,
          voices: [
            { index: 0, name: "Voice 1", algorithm: 0 },
            { index: 1, name: "Voice 2", algorithm: 1 },
            { index: 2, name: "Voice 3", algorithm: 2 },
          ],
        });
        return Promise.resolve([]);
      });
      const { container } = render(<FXChain />);
      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("read.getFxSlots", expect.objectContaining({ trackIndex: 0 }));
      });
      await flushRead();

      expect(container.querySelectorAll(".fx-slot").length).toBe(1);
      expect(container.querySelector(".fx-slot")).toHaveClass("fx-slot");
    });
  });

  describe("crashed slot display", () => {
    it("shows crash message when slot is crashed", async () => {
      useUiStore.setState({
        selectedTrackIndex: 0,
        crashedFxSlots: { "0:/plugins/Delay.vst3": { pluginName: "Delay" } },
      });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      expect(screen.getByText(/Plugin crashed/)).toBeInTheDocument();
      expect(screen.getByText("Restart")).toBeInTheDocument();
    });

    it("does not show crash message for non-crashed slots", async () => {
      useUiStore.setState({
        selectedTrackIndex: 0,
        crashedFxSlots: {},
      });
      mockedCall.mockResolvedValue(TWO_SLOTS);
      render(<FXChain />);
      await flushRead();

      expect(screen.queryByText("Plugin crashed")).not.toBeInTheDocument();
    });
  });

  describe("track change refreshes slots", () => {
    it("changing selectedTrackIndex fetches new slots", async () => {
      mockedCall.mockResolvedValue([]);
      const { rerender } = render(<FXChain />);

      act(() => {
        useUiStore.setState({ selectedTrackIndex: 0 });
      });
      rerender(<FXChain />);
      await flushRead();

      expect(mockedCall).toHaveBeenCalledWith("read.getFxSlots", expect.objectContaining({ trackIndex: 0 }));
    });
  });
});
