import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import PreferencesDialog from "./PreferencesDialog";
import { rpc } from "../rpc";
import { useLibraryStore } from "../store/libraryStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn() },
}));

vi.mock("../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function setupRpcDefaults() {
  mockedCall.mockImplementation(async (method: string) => {
    switch (method) {
      case "audio.getDeviceTypes":
        return ["WASAPI", "DirectSound"];
      case "audio.getCurrentSetup":
        return { driver: "WASAPI", output: "Speakers", input: "Mic", sampleRate: 48000, bufferSize: 256, latencyMs: 12.3 };
      case "audio.getOutputDevices":
        return ["Speakers", "Headphones"];
      case "audio.getInputDevices":
        return ["Mic", "Line In"];
      case "audio.getSampleRates":
        return [44100, 48000, 96000];
      case "audio.getBufferSizes":
        return [128, 256, 512, 1024];
      case "settings.getDefaultTempo":
        return 120;
      case "settings.getDefaultTimeSignature":
        return { numerator: 4, denominator: 4 };
      case "settings.getMaxBackups":
        return 10;
      case "plugin.getIsolationEnabled":
        return true;
      case "plugin.getWatchPlugins":
        return true;
      case "midi.getAvailableDevices":
        return ["MIDI Keyboard"];
      case "midi.getOpenDevice":
        return "MIDI Keyboard";
      case "library.list":
        return [];
      default:
        return null;
    }
  });
}

describe("PreferencesDialog", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    setupRpcDefaults();
    useLibraryStore.setState({ libraries: [], loading: false });
  });

  afterEach(() => {
    cleanup();
  });

  it("renders without crashing", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    expect(screen.getByText("Preferences")).toBeInTheDocument();
  });

  it("shows the Audio section", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    const matches = screen.getAllByText("Audio");
    expect(matches.length).toBeGreaterThanOrEqual(1);
    expect(matches[0].tagName).toBe("H3");
  });

  it("shows the General section", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    expect(screen.getByText("General")).toBeInTheDocument();
  });

  it("shows the MIDI section", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    const matches = screen.getAllByText("MIDI");
    expect(matches.length).toBeGreaterThanOrEqual(1);
    expect(matches[0].tagName).toBe("H3");
  });

  it("shows the Plugins section", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    expect(screen.getByText("Plugins")).toBeInTheDocument();
  });

  it("shows the Engine Connection section", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    expect(screen.getByText("Engine Connection")).toBeInTheDocument();
  });

  it("Engine Connection shows a WebSocket endpoint (ws://)", () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    const wsText = screen.getByText(/ws:\/\//);
    expect(wsText).toBeInTheDocument();
  });

  it("Close button calls onClose", async () => {
    const onClose = vi.fn();
    render(<PreferencesDialog onClose={onClose} />);
    const closeBtn = await screen.findByRole("button", { name: "×" });
    fireEvent.click(closeBtn);
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it("Audio driver type select is present", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await waitFor(() => {
      expect(screen.getByRole("option", { name: "WASAPI" })).toBeInTheDocument();
    });
    expect(screen.getByRole("option", { name: "DirectSound" })).toBeInTheDocument();
  });

  it("Sample rate select shows standard rates (48000 Hz)", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await waitFor(() => {
      expect(screen.getByRole("option", { name: "48000 Hz" })).toBeInTheDocument();
    });
  });

  it("Buffer size select is present (256)", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await waitFor(() => {
      expect(screen.getByRole("option", { name: "256 samples" })).toBeInTheDocument();
    });
  });

  it("Loads preferences via RPC on mount", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("midi.getAvailableDevices");
      expect(mockedCall).toHaveBeenCalledWith("midi.getOpenDevice");
      expect(mockedCall).toHaveBeenCalledWith("audio.getDeviceTypes");
      expect(mockedCall).toHaveBeenCalledWith("audio.getCurrentSetup");
      expect(mockedCall).toHaveBeenCalledWith("settings.getDefaultTempo");
      expect(mockedCall).toHaveBeenCalledWith("settings.getDefaultTimeSignature");
      expect(mockedCall).toHaveBeenCalledWith("settings.getMaxBackups");
      expect(mockedCall).toHaveBeenCalledWith("plugin.getIsolationEnabled");
      expect(mockedCall).toHaveBeenCalledWith("plugin.getWatchPlugins");
    });
  });
});
