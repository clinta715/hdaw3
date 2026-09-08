import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, cleanup, act, fireEvent } from "@testing-library/react";
import NeuralPanel from "../NeuralPanel";
import { rpc } from "../../rpc";
import { useNeuralStore } from "../../store/neuralStore";
import { useUiStore } from "../../store/uiStore";
import { useTransportStore } from "../../store/transportStore";

vi.mock("../../rpc", () => ({ rpc: { call: vi.fn(), onNotification: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;
const mockedOnNotification = rpc.onNotification as unknown as ReturnType<typeof vi.fn>;

type NotifHandler = (method: string, params: unknown) => void;
let notifHandlers: Record<string, NotifHandler>;

async function flush() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
    await Promise.resolve();
  });
}

const MODELS = {
  models: [
    { name: "Model A", path: "C:/rave/models/a.onnx", extension: ".onnx", sizeBytes: 1000 },
    { name: "Model B", path: "C:/rave/models/b.pts", extension: ".pts", sizeBytes: 2000 },
  ],
};

function setPendingClip(overrides: Record<string, unknown> = {}) {
  useNeuralStore.getState().setPendingClip({
    clipId: 7,
    name: "Kick Loop",
    sourceFile: "C:/audio/kick.wav",
    trackIndex: 2,
    startBeat: 8,
    ...overrides,
  });
}

async function renderPanel(modelsResult: unknown = MODELS, raveConfig: unknown = undefined) {
  mockedCall.mockImplementation((method: string) => {
    if (method === "rave.listModels") return Promise.resolve(modelsResult);
    if (method === "settings.getRaveConfig") {
      return raveConfig === "reject"
        ? Promise.reject(new Error("unknown method"))
        : Promise.resolve(raveConfig);
    }
    if (method === "rave.probeModel") {
      return Promise.resolve({
        ok: true,
        methods: ["encode", "decode"],
        sampleRate: null,
        latentDim: 16,
        latentFrames: 22,
        encodeShape: [1, 16, 22],
        decodeShape: [1, 2, 45056],
        error: "",
      });
    }
    return Promise.resolve(undefined);
  });
  const utils = render(<NeuralPanel />);
  await flush();
  return utils;
}

function fireProgress(params: Record<string, unknown>) {
  act(() => {
    notifHandlers["notify.raveProgress"]?.("notify.raveProgress", params);
  });
}

function fireTrainingProgress(params: Record<string, unknown>) {
  act(() => {
    notifHandlers["notify.raveTrainingProgress"]?.("notify.raveTrainingProgress", params);
  });
}

function getButton(text: string): HTMLButtonElement {
  const btns = screen.queryAllByRole("button", { name: text });
  expect(btns.length, `button "${text}" not found`).toBeGreaterThan(0);
  return btns[0] as HTMLButtonElement;
}

describe("NeuralPanel", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedOnNotification.mockReset();
    notifHandlers = {};
    mockedOnNotification.mockImplementation((method: string, handler: NotifHandler) => {
      notifHandlers[method] = handler;
      return () => { delete notifHandlers[method]; };
    });
    useNeuralStore.getState().clearPendingClip();
    useUiStore.setState({ selectedTrackIndex: 1 });
    useTransportStore.setState({
      transport: { ...useTransportStore.getState().transport, bpm: 120, currentTimeSeconds: 30 },
    });
  });

  afterEach(() => cleanup());

  it("(a) loads models via rave.listModels and renders them in the select", async () => {
    const { container } = await renderPanel();
    expect(mockedCall).toHaveBeenCalledWith("rave.listModels", {});
    const select = container.querySelector(".neural-panel__select") as HTMLSelectElement;
    expect(select).toBeTruthy();
    const options = Array.from(select.options).map((o) => o.textContent);
    expect(options).toContain("Model A");
    expect(options).toContain("Model B");
    // First model auto-selected.
    expect(select.value).toBe("C:/rave/models/a.onnx");
  });


  it("(a2) probes the selected model and displays offline metadata", async () => {
    await renderPanel();
    expect(mockedCall).toHaveBeenCalledWith("rave.probeModel", { modelPath: "C:/rave/models/a.onnx" });
    expect(screen.getByText("Probe OK")).toBeTruthy();
    expect(screen.getByText("Methods: encode / decode")).toBeTruthy();
    expect(screen.getByText(/Latent: 16 × 22/)).toBeTruthy();
    expect(screen.getByText(/SR: unknown/)).toBeTruthy();
  });

  it("(b) shows a friendly empty state when no models are found", async () => {
    await renderPanel({ models: [] });
    expect(screen.getByText(/no rave models found/i)).toBeTruthy();
  });

  it("(c) Start calls rave.startTransform with the pending clip input and shows running state", async () => {
    setPendingClip();
    await renderPanel();
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 42 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.startTransform", {
      inputPath: "C:/audio/kick.wav",
      modelPath: "C:/rave/models/a.onnx",
      outputPath: "compositions/Kick Loop_rave.wav",
      temperature: 1.0,
      seed: 0,
    });
    // Running state: Rendering… label + Cancel button visible.
    expect(getButton("Rendering…")).toBeTruthy();
    expect(getButton("Cancel")).toBeTruthy();
  });

  it("(d) a finished notification reveals the Import controls", async () => {
    setPendingClip();
    await renderPanel();
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 42 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    expect(screen.queryByText("Import Result")).toBeNull();
    fireProgress({ jobId: 42, state: "finished", message: "done" });
    expect(screen.getByText("Import Result")).toBeTruthy();
    expect(screen.getByText("compositions/Kick Loop_rave.wav")).toBeTruthy();
    expect(getButton("Import")).toBeTruthy();
  });

  it("(d2) ignores progress notifications for other job ids", async () => {
    setPendingClip();
    await renderPanel();
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 42 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    fireProgress({ jobId: 99, state: "finished", message: "other job" });
    expect(screen.queryByText("Import Result")).toBeNull();
  });

  it("(e) Import calls rave.importResult with startBeats in BEATS and alignToGrid false by default", async () => {
    setPendingClip(); // startBeat 8, trackIndex 2
    await renderPanel();
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 42 });
      if (method === "rave.importResult") return Promise.resolve({ clipId: 101, samplerOk: false, samplerError: "" });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    fireProgress({ jobId: 42, state: "finished", message: "done" });
    fireEvent.click(getButton("Import"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.importResult", {
      outputPath: "compositions/Kick Loop_rave.wav",
      trackIndex: 2,
      startBeats: 8, // BEATS from the pending clip — never seconds
      alignToGrid: false,
    });
  });

  it("(e2) with no pending clip, start beats default to the playhead in beats and sampler indices are omitted", async () => {
    await renderPanel();
    // Default output path with no clip:
    const outputInput = screen.getByDisplayValue("compositions/rave_render.wav");
    expect(outputInput).toBeTruthy();
    const inputEl = screen.getByPlaceholderText(/input wav path/i) as HTMLInputElement;
    fireEvent.change(inputEl, { target: { value: "C:/audio/free.wav" } });
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 5 });
      if (method === "rave.importResult") return Promise.resolve({ clipId: 1 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    fireProgress({ jobId: 5, state: "finished", message: "done" });
    fireEvent.click(getButton("Import"));
    await flush();
    // playhead: 30s at 120bpm = 60 beats; selectedTrackIndex 1.
    expect(mockedCall).toHaveBeenCalledWith("rave.importResult", {
      outputPath: "compositions/rave_render.wav",
      trackIndex: 1,
      startBeats: 60,
      alignToGrid: false,
    });
  });

  it("(f) Cancel calls rave.cancelJob with the running job id", async () => {
    setPendingClip();
    await renderPanel();
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.listModels") return Promise.resolve(MODELS);
      if (method === "rave.startTransform") return Promise.resolve({ jobId: 42 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Transform"));
    await flush();
    fireEvent.click(getButton("Cancel"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.cancelJob", { jobId: 42 });
  });

  it("(g) defaultModel from settings.getRaveConfig preselects the matching model", async () => {
    const { container } = await renderPanel(MODELS, {
      modelDirs: ["C:/rave/models"],
      defaultModel: "C:/rave/models/b.pts",
      pythonPath: "",
      scriptPath: "",
      timeoutMs: 0,
    });
    expect(mockedCall).toHaveBeenCalledWith("settings.getRaveConfig", {});
    const select = container.querySelector(".neural-panel__select") as HTMLSelectElement;
    expect(select).toBeTruthy();
    expect(select.value).toBe("C:/rave/models/b.pts");
  });

  it("(g2) a default model not among loaded models is added as an extra option and selected", async () => {
    const { container } = await renderPanel(MODELS, {
      modelDirs: [],
      defaultModel: "C:/other/deep.onnx",
      pythonPath: "",
      scriptPath: "",
      timeoutMs: 0,
    });
    const select = container.querySelector(".neural-panel__select") as HTMLSelectElement;
    const options = Array.from(select.options).map((o) => o.textContent);
    expect(options).toContain("Model A");
    expect(options).toContain("Model B");
    expect(options).toContain("deep.onnx (default)");
    expect(select.value).toBe("C:/other/deep.onnx");
  });

  it("(h) getRaveConfig rejection degrades silently — models still render, first selected", async () => {
    const { container } = await renderPanel(MODELS, "reject");
    const select = container.querySelector(".neural-panel__select") as HTMLSelectElement;
    expect(select).toBeTruthy();
    const options = Array.from(select.options).map((o) => o.textContent);
    expect(options).toContain("Model A");
    expect(options).toContain("Model B");
    expect(select.value).toBe("C:/rave/models/a.onnx");
  });

  it("(i) Start Training calls rave.startTraining with training payload and shows running state", async () => {
    await renderPanel();
    fireEvent.change(screen.getByPlaceholderText(/dataset folder/i), { target: { value: "C:/data/rave" } });
    fireEvent.change(screen.getByDisplayValue("rave/models/trained_model.ts"), { target: { value: "C:/rave/out.ts" } });
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.startTraining") return Promise.resolve({ jobId: 77 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Training"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.startTraining", {
      datasetPath: "C:/data/rave",
      outputModelPath: "C:/rave/out.ts",
      name: "trained_model",
      epochs: 10,
      batchSize: 8,
      sampleRate: 44100,
    });
    expect(getButton("Training…")).toBeTruthy();
    expect(getButton("Cancel Training")).toBeTruthy();
  });

  it("(j) training progress updates status and ignores unrelated ids", async () => {
    await renderPanel();
    fireEvent.change(screen.getByPlaceholderText(/dataset folder/i), { target: { value: "C:/data/rave" } });
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.startTraining") return Promise.resolve({ jobId: 77 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Training"));
    await flush();
    fireTrainingProgress({ jobId: 99, state: "finished", message: "other" });
    expect(screen.queryByText("other")).toBeNull();
    fireTrainingProgress({ jobId: 77, state: "finished", message: "done" });
    expect(screen.getByText("finished")).toBeTruthy();
    expect(screen.getByText("done")).toBeTruthy();
  });

  it("(k) Cancel Training calls rave.cancelTrainingJob with the running training job id", async () => {
    await renderPanel();
    fireEvent.change(screen.getByPlaceholderText(/dataset folder/i), { target: { value: "C:/data/rave" } });
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.startTraining") return Promise.resolve({ jobId: 77 });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Training"));
    await flush();
    fireEvent.click(getButton("Cancel Training"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.cancelTrainingJob", { jobId: 77 });
  });

  it("(l) Status polls rave.trainingJobStatus and updates training message", async () => {
    await renderPanel();
    fireEvent.change(screen.getByPlaceholderText(/dataset folder/i), { target: { value: "C:/data/rave" } });
    mockedCall.mockImplementation((method: string) => {
      if (method === "rave.startTraining") return Promise.resolve({ jobId: 77 });
      if (method === "rave.trainingJobStatus") return Promise.resolve({ state: "running", message: "epoch 1" });
      return Promise.resolve(undefined);
    });
    fireEvent.click(getButton("Start Training"));
    await flush();
    fireEvent.click(getButton("Status"));
    await flush();
    expect(mockedCall).toHaveBeenCalledWith("rave.trainingJobStatus", { jobId: 77 });
    expect(screen.getByText("epoch 1")).toBeTruthy();
  });

  it("subscribes to RAVE notifications and unsubscribes on unmount", async () => {
    const { unmount } = await renderPanel();
    expect(mockedOnNotification).toHaveBeenCalledWith("notify.raveProgress", expect.any(Function));
    expect(mockedOnNotification).toHaveBeenCalledWith("notify.raveTrainingProgress", expect.any(Function));
    unmount();
    expect(notifHandlers["notify.raveProgress"]).toBeUndefined();
    expect(notifHandlers["notify.raveTrainingProgress"]).toBeUndefined();
  });
});
