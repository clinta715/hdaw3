import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import PreferencesDialog from "../PreferencesDialog";
import { rpc } from "../../rpc";
import { useLibraryStore } from "../../store/libraryStore";

vi.mock("../../rpc", () => ({
  rpc: { call: vi.fn() },
}));

vi.mock("../../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

const RAVE_CONFIG = {
  modelDirs: ["C:/rave/models", "D:/extra/models"],
  defaultModel: "C:/rave/models/a.onnx",
  pythonPath: "C:/python/python.exe",
  scriptPath: "C:/rave/infer.py",
  timeoutMs: 900000,
};

const RAVE_MODELS = {
  models: [
    { name: "Model A", path: "C:/rave/models/a.onnx", extension: ".onnx", sizeBytes: 1000 },
    { name: "Model B", path: "C:/rave/models/b.pts", extension: ".pts", sizeBytes: 2000 },
  ],
};

function setupRpc(overrides: Record<string, unknown> = {}) {
  mockedCall.mockImplementation(async (method: string) => {
    switch (method) {
      case "settings.getRaveConfig":
        return "getRaveConfig" in overrides ? overrides.getRaveConfig : RAVE_CONFIG;
      case "rave.listModels":
        return RAVE_MODELS;
      case "settings.setRaveConfig":
        return null;
      case "audio.getDeviceTypes":
        return ["WASAPI"];
      case "audio.getCurrentSetup":
        return { driver: "WASAPI", output: "Speakers", input: "Mic", sampleRate: 48000, bufferSize: 256, latencyMs: 12.3 };
      case "audio.getOutputDevices":
        return ["Speakers"];
      case "audio.getInputDevices":
        return ["Mic"];
      case "audio.getSampleRates":
        return [48000];
      case "audio.getBufferSizes":
        return [256];
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
      case "settings.getMcpHttpConfig":
        return { enabled: false, host: "127.0.0.1", port: 18765, running: false, lastError: "" };
      case "midi.getAvailableDevices":
        return [];
      case "midi.getOpenDevice":
        return "";
      case "library.list":
        return [];
      default:
        return null;
    }
  });
}

function getAddDirRow(): HTMLElement {
  const input = screen.getByPlaceholderText("Model directory path");
  const row = input.closest(".pref-library-add");
  expect(row).toBeTruthy();
  return row as HTMLElement;
}

function getSaveButton(): HTMLButtonElement {
  return screen.getByRole("button", { name: "Save" }) as HTMLButtonElement;
}

function getDefaultModelSelect(): HTMLSelectElement {
  const opt = screen.getByRole("option", { name: "Model A" });
  return opt.parentElement as HTMLSelectElement;
}

describe("PreferencesDialog RAVE section", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    setupRpc();
    useLibraryStore.setState({ libraries: [], loading: false });
  });

  afterEach(() => {
    cleanup();
  });

  it("renders the RAVE section and loads current config on mount", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    const heading = await screen.findByRole("heading", { name: "RAVE" });
    expect(heading).toBeInTheDocument();
    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("settings.getRaveConfig", {});
      expect(mockedCall).toHaveBeenCalledWith("rave.listModels", {});
    });
    // Config values populated into the fields.
    await waitFor(() => {
      expect(screen.getByDisplayValue("C:/python/python.exe")).toBeInTheDocument();
    });
    expect(screen.getByDisplayValue("C:/rave/infer.py")).toBeInTheDocument();
    expect(screen.getByDisplayValue("900000")).toBeInTheDocument();
    // Model dirs rendered as rows.
    expect(screen.getByTitle("C:/rave/models")).toBeInTheDocument();
    expect(screen.getByTitle("D:/extra/models")).toBeInTheDocument();
    // Default model select reflects the config; models from rave.listModels.
    const select = getDefaultModelSelect();
    expect(select.value).toBe("C:/rave/models/a.onnx");
    expect(screen.getByRole("option", { name: "Model B" })).toBeInTheDocument();
  });

  it("degrades silently when settings.getRaveConfig rejects (old engine)", async () => {
    setupRpc({ getRaveConfig: Promise.reject(new Error("unknown method")) });
    render(<PreferencesDialog onClose={vi.fn()} />);
    const heading = await screen.findByRole("heading", { name: "RAVE" });
    expect(heading).toBeInTheDocument();
    // No crash, empty fields, and the "(none)" default option is present.
    expect(screen.getByPlaceholderText("Model directory path")).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "(none)" })).toBeInTheDocument();
  });

  it("add and remove model dir rows update the list", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await screen.findByRole("heading", { name: "RAVE" });
    await waitFor(() => {
      expect(screen.getByTitle("C:/rave/models")).toBeInTheDocument();
    });

    // Add a new dir.
    const addRow = getAddDirRow();
    const input = addRow.querySelector("input") as HTMLInputElement;
    fireEvent.change(input, { target: { value: "E:/new/models" } });
    fireEvent.click(addRow.querySelector("button") as HTMLButtonElement);
    await waitFor(() => {
      expect(screen.getByTitle("E:/new/models")).toBeInTheDocument();
    });
    expect((input as HTMLInputElement).value).toBe("");

    // Remove it again via its row's Remove button.
    const row = screen.getByTitle("E:/new/models").closest(".pref-library-row") as HTMLElement;
    fireEvent.click(row.querySelector(".pref-btn-danger") as HTMLButtonElement);
    await waitFor(() => {
      expect(screen.queryByTitle("E:/new/models")).toBeNull();
    });
    // Existing dirs untouched.
    expect(screen.getByTitle("C:/rave/models")).toBeInTheDocument();
  });

  it("Save calls settings.setRaveConfig with all five fields including edits", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await screen.findByRole("heading", { name: "RAVE" });
    await waitFor(() => {
      expect(screen.getByDisplayValue("C:/python/python.exe")).toBeInTheDocument();
    });

    // Edit python path, timeout, and the default model selection.
    fireEvent.change(screen.getByDisplayValue("C:/python/python.exe"), {
      target: { value: "C:/Python312/python.exe" },
    });
    fireEvent.change(screen.getByDisplayValue("900000"), {
      target: { value: "1200000" },
    });
    fireEvent.change(getDefaultModelSelect(), {
      target: { value: "C:/rave/models/b.pts" },
    });

    fireEvent.click(getSaveButton());
    await waitFor(() => {
      expect(mockedCall).toHaveBeenCalledWith("settings.setRaveConfig", {
        modelDirs: ["C:/rave/models", "D:/extra/models"],
        defaultModel: "C:/rave/models/b.pts",
        pythonPath: "C:/Python312/python.exe",
        scriptPath: "C:/rave/infer.py",
        timeoutMs: 1200000,
      });
    });
  });

  it("Save reloads values from the engine (no optimistic state)", async () => {
    render(<PreferencesDialog onClose={vi.fn()} />);
    await screen.findByRole("heading", { name: "RAVE" });
    await waitFor(() => {
      expect(screen.getByDisplayValue("C:/python/python.exe")).toBeInTheDocument();
    });
    const getCallsBefore = mockedCall.mock.calls.filter(
      (c) => c[0] === "settings.getRaveConfig"
    ).length;
    fireEvent.click(getSaveButton());
    await waitFor(() => {
      const getCallsAfter = mockedCall.mock.calls.filter(
        (c) => c[0] === "settings.getRaveConfig"
      ).length;
      expect(getCallsAfter).toBeGreaterThan(getCallsBefore);
    });
  });
});
