import React, { useState, useEffect, useRef, useCallback } from "react";
import { rpc } from "../rpc";
import { useNeuralStore } from "../store/neuralStore";
import { useUiStore } from "../store/uiStore";
import { useTransportStore } from "../store/transportStore";
import { useNotifyStore, reportRpcError } from "../store/notifyStore";
import "./NeuralPanel.css";

interface RaveModel {
  name: string;
  path: string;
  extension?: string;
  sizeBytes?: number;
}

interface RaveProbeMetadata {
  ok?: boolean;
  methods?: string[];
  sampleRate?: number | null;
  latentDim?: number | null;
  latentFrames?: number | null;
  encodeShape?: number[] | null;
  decodeShape?: number[] | null;
  error?: string;
}

type ProbeState =
  | { phase: "idle" }
  | { phase: "loading" }
  | { phase: "success"; data: RaveProbeMetadata }
  | { phase: "error"; error: string };

// Job lifecycle strings emitted by the engine's RaveJobManager on
// notify.raveProgress: "running" | "finished" | "failed" | "cancelled".
type JobPhase = "idle" | "running" | "finished" | "failed" | "cancelled";

function sanitizeForFileName(name: string): string {
  return name.trim().replace(/[\\/:*?"<>|]+/g, "_");
}

function defaultOutputPath(clipName: string | null): string {
  const base = clipName && clipName.trim().length > 0
    ? `${sanitizeForFileName(clipName)}_rave.wav`
    : "rave_render.wav";
  return `compositions/${base}`;
}

export default function NeuralPanel() {
  const pendingClip = useNeuralStore((s) => s.pendingClip);

  const [models, setModels] = useState<RaveModel[] | null>(null);
  const [modelPath, setModelPath] = useState("");
  const [inputPath, setInputPath] = useState("");
  const [outputPath, setOutputPath] = useState(() => defaultOutputPath(null));
  const [temperature, setTemperature] = useState(1.0);
  const [seed, setSeed] = useState(0);
  const [probeState, setProbeState] = useState<ProbeState>({ phase: "idle" });

  const [phase, setPhase] = useState<JobPhase>("idle");
  const [jobMessage, setJobMessage] = useState("");
  // jobId lives in a ref so the notification handler (registered once) never
  // closes over a stale id — AGENTS frontend pitfalls 1–5.
  const jobIdRef = useRef<number | null>(null);

  // Import controls (revealed on "finished"; import is an explicit user act,
  // never automatic).
  const [importTrack, setImportTrack] = useState(0);
  const [importStartBeats, setImportStartBeats] = useState(0);
  const [alignToGrid, setAlignToGrid] = useState(false);
  const [samplerTrack, setSamplerTrack] = useState(-1);
  const [samplerSlot, setSamplerSlot] = useState(-1);

  const [datasetPath, setDatasetPath] = useState("");
  const [trainingOutputPath, setTrainingOutputPath] = useState("rave/models/trained_model.ts");
  const [trainingName, setTrainingName] = useState("trained_model");
  const [trainingEpochs, setTrainingEpochs] = useState(10);
  const [trainingBatchSize, setTrainingBatchSize] = useState(8);
  const [trainingSampleRate, setTrainingSampleRate] = useState(44100);
  const [trainingPhase, setTrainingPhase] = useState<JobPhase>("idle");
  const [trainingMessage, setTrainingMessage] = useState("");
  const trainingJobIdRef = useRef<number | null>(null);

  // Prefill input/output paths from the pending-clip handoff.
  useEffect(() => {
    if (pendingClip) {
      setInputPath(pendingClip.sourceFile);
      setOutputPath(defaultOutputPath(pendingClip.name));
    }
  }, [pendingClip]);

  // Load the model list once on mount. Never hardcoded — always fetched.
  // Also fetch settings.getRaveConfig for the default-model preselect; this
  // degrades SILENTLY (no toast) on old engines without the RPC.
  useEffect(() => {
    let cancelled = false;
    const load = async () => {
      let defaultModel = "";
      try {
        const cfg = await rpc.call("settings.getRaveConfig", {});
        defaultModel = (cfg as { defaultModel?: string } | null)?.defaultModel ?? "";
      } catch {
        defaultModel = ""; // old engine / unset — keep current behavior
      }
      let list: RaveModel[] = [];
      let listFailed = false;
      try {
        const data = await rpc.call("rave.listModels", {});
        list = ((data as { models?: RaveModel[] } | null)?.models) ?? [];
      } catch (err) {
        listFailed = true;
        if (!cancelled) reportRpcError("rave.listModels", err);
      }
      if (cancelled) return;
      if (listFailed) {
        setModels([]);
        return;
      }
      // If the configured default model isn't among the scanned models, add it
      // as an extra option (labeled with its file name) so it stays selectable.
      let finalList = list;
      if (defaultModel && !list.some((m) => m.path === defaultModel)) {
        const fileName = defaultModel.split(/[\\/]/).pop() || defaultModel;
        finalList = [...list, { name: `${fileName} (default)`, path: defaultModel }];
      }
      setModels(finalList);
      if (defaultModel) {
        setModelPath(defaultModel);
      } else if (finalList.length > 0) {
        setModelPath((prev) => prev || finalList[0].path);
      }
    };
    void load();
    return () => { cancelled = true; };
  }, []);

  // Probe the selected model for offline metadata only. This intentionally
  // does not enable latent-edit controls yet; it degrades quietly on older
  // engines that do not expose rave.probeModel.
  useEffect(() => {
    if (!modelPath) {
      setProbeState({ phase: "idle" });
      return;
    }
    let cancelled = false;
    setProbeState({ phase: "loading" });
    const run = async () => {
      try {
        const data = (await rpc.call("rave.probeModel", { modelPath })) as RaveProbeMetadata | null;
        if (cancelled) return;
        if (data?.ok) {
          setProbeState({ phase: "success", data });
        } else {
          setProbeState({ phase: "error", error: data?.error || "RAVE probe failed" });
        }
      } catch (err) {
        if (cancelled) return;
        const message = err instanceof Error ? err.message : String(err);
        setProbeState({ phase: "error", error: message || "RAVE probe unavailable" });
      }
    };
    void run();
    return () => { cancelled = true; };
  }, [modelPath]);

  // Subscribe to job progress once; unsubscribe on unmount.
  useEffect(() => {
    const off = rpc.onNotification("notify.raveProgress", (_method, params) => {
      const p = (params ?? {}) as { jobId?: number; state?: string; message?: string };
      if (p.jobId == null || jobIdRef.current == null || p.jobId !== jobIdRef.current) return;
      const st = String(p.state ?? "");
      setJobMessage(p.message ?? "");
      if (st === "finished") {
        setPhase("finished");
        // Prefill import defaults from FRESH store state (never closure props).
        const clip = useNeuralStore.getState().pendingClip;
        const selectedTrackIndex = useUiStore.getState().selectedTrackIndex;
        const tr = useTransportStore.getState().transport;
        setImportTrack(clip ? clip.trackIndex : (selectedTrackIndex ?? 0));
        // Playhead in BEATS (beats-vs-seconds convention): seconds * bpm/60.
        setImportStartBeats(clip ? clip.startBeat : tr.currentTimeSeconds * (tr.bpm / 60));
      } else if (st === "failed") {
        setPhase("failed");
        useNotifyStore.getState().push({
          level: "error",
          message: `RAVE job failed: ${p.message ?? "unknown error"}`,
        });
      } else if (st === "cancelled") {
        setPhase("cancelled");
      } else {
        setPhase("running");
      }
    });
    return off;
  }, []);

  useEffect(() => {
    const off = rpc.onNotification("notify.raveTrainingProgress", (_method, params) => {
      const p = (params ?? {}) as { jobId?: number; state?: string; message?: string };
      if (p.jobId == null || trainingJobIdRef.current == null || p.jobId !== trainingJobIdRef.current) return;
      const st = String(p.state ?? "");
      setTrainingMessage(p.message ?? "");
      if (st === "finished") {
        setTrainingPhase("finished");
        useNotifyStore.getState().push({ level: "success", message: "RAVE training finished" });
      } else if (st === "failed") {
        setTrainingPhase("failed");
        useNotifyStore.getState().push({
          level: "error",
          message: `RAVE training failed: ${p.message ?? "unknown error"}`,
        });
      } else if (st === "cancelled") {
        setTrainingPhase("cancelled");
      } else {
        setTrainingPhase("running");
      }
    });
    return off;
  }, []);

  const startTransform = useCallback(async () => {
    if (!modelPath || !inputPath || !outputPath) return;
    jobIdRef.current = null;
    setPhase("running");
    setJobMessage("Starting…");
    try {
      // Async sidecar job — the 30s RPC timeout makes sync transformFile/
      // transformClip unusable for renders. Progress arrives via
      // notify.raveProgress.
      const res = await rpc.call("rave.startTransform", {
        inputPath,
        modelPath,
        outputPath,
        temperature,
        seed,
      });
      const jobId = (res as { jobId?: number } | null)?.jobId ?? null;
      jobIdRef.current = jobId;
      if (jobId == null) {
        setPhase("failed");
        setJobMessage("Engine returned no jobId");
      }
    } catch (err) {
      setPhase("failed");
      reportRpcError("rave.startTransform", err);
    }
  }, [modelPath, inputPath, outputPath, temperature, seed]);

  const cancelJob = useCallback(async () => {
    const jobId = jobIdRef.current;
    if (jobId == null) return;
    try {
      await rpc.call("rave.cancelJob", { jobId });
      // Phase flips to "cancelled" via the notify.raveProgress push.
    } catch (err) {
      reportRpcError("rave.cancelJob", err);
    }
  }, []);

  const startTraining = useCallback(async () => {
    if (!datasetPath || !trainingOutputPath) return;
    trainingJobIdRef.current = null;
    setTrainingPhase("running");
    setTrainingMessage("Starting…");
    try {
      const res = await rpc.call("rave.startTraining", {
        datasetPath,
        outputModelPath: trainingOutputPath,
        name: trainingName,
        epochs: trainingEpochs,
        batchSize: trainingBatchSize,
        sampleRate: trainingSampleRate,
      });
      const jobId = (res as { jobId?: number } | null)?.jobId ?? null;
      trainingJobIdRef.current = jobId;
      if (jobId != null) setTrainingMessage(`Training job #${jobId}`);
      if (jobId == null) {
        setTrainingPhase("failed");
        setTrainingMessage("Engine returned no training jobId");
      }
    } catch (err) {
      setTrainingPhase("failed");
      reportRpcError("rave.startTraining", err);
    }
  }, [datasetPath, trainingOutputPath, trainingName, trainingEpochs, trainingBatchSize, trainingSampleRate]);

  const refreshTrainingStatus = useCallback(async () => {
    const jobId = trainingJobIdRef.current;
    if (jobId == null) return;
    try {
      const status = (await rpc.call("rave.trainingJobStatus", { jobId })) as { state?: string; message?: string } | null;
      const st = String(status?.state ?? "");
      setTrainingMessage(status?.message ?? "");
      if (st === "running" || st === "finished" || st === "failed" || st === "cancelled") {
        setTrainingPhase(st);
      }
    } catch (err) {
      reportRpcError("rave.trainingJobStatus", err);
    }
  }, []);

  const cancelTrainingJob = useCallback(async () => {
    const jobId = trainingJobIdRef.current;
    if (jobId == null) return;
    try {
      await rpc.call("rave.cancelTrainingJob", { jobId });
    } catch (err) {
      reportRpcError("rave.cancelTrainingJob", err);
    }
  }, []);

  const importResult = useCallback(async () => {
    if (!outputPath) return;
    const params: Record<string, unknown> = {
      outputPath,
      trackIndex: importTrack,
      startBeats: importStartBeats, // BEATS — the engine converts to seconds.
      alignToGrid,
    };
    // Sampler send is opt-in: only sent when both indices are >= 0.
    if (samplerTrack >= 0 && samplerSlot >= 0) {
      params.samplerTrackIndex = samplerTrack;
      params.samplerSlotIndex = samplerSlot;
    }
    try {
      await rpc.call("rave.importResult", params);
      // Clip appears via the engine's delta/fullSync push — no optimistic
      // placement here (AGENTS frontend pitfalls 2/3/6).
      useNotifyStore.getState().push({ level: "success", message: "RAVE render imported" });
    } catch (err) {
      reportRpcError("rave.importResult", err);
    }
  }, [outputPath, importTrack, importStartBeats, alignToGrid, samplerTrack, samplerSlot]);

  const canStart = phase !== "running" && !!modelPath && !!inputPath && !!outputPath;
  const canStartTraining = trainingPhase !== "running" && !!datasetPath && !!trainingOutputPath;
  const modelsLoaded = models !== null;

  return (
    <div className="neural-panel">
      <div className="neural-panel__col">
        <div className="neural-panel__title">RAVE Neural Render</div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Model</label>
          {modelsLoaded && models!.length === 0 ? (
            <span className="neural-panel__empty">
              No RAVE models found — add model files (.onnx/.pts) to the RAVE models directory.
            </span>
          ) : (
            <select
              className="neural-panel__select"
              value={modelPath}
              onChange={(e) => setModelPath(e.target.value)}
              disabled={!modelsLoaded || (models ?? []).length === 0}
            >
              {!modelsLoaded && <option value="">Loading…</option>}
              {(models ?? []).map((m) => (
                <option key={m.path} value={m.path}>{m.name}</option>
              ))}
            </select>
          )}
        </div>

        {modelPath && (
          <div className="neural-panel__probe" aria-label="RAVE model probe">
            {probeState.phase === "loading" && <span className="neural-panel__empty">Probing model…</span>}
            {probeState.phase === "success" && (
              <>
                <span className="neural-panel__badge">Probe OK</span>
                <span>Methods: {(probeState.data.methods ?? []).filter((m) => m === "encode" || m === "decode").join(" / ") || "—"}</span>
                <span>Latent: {probeState.data.latentDim ?? "?"} × {probeState.data.latentFrames ?? "?"}</span>
                <span>SR: {probeState.data.sampleRate ?? "unknown"}</span>
              </>
            )}
            {probeState.phase === "error" && (
              <span className="neural-panel__probe-error">Probe unavailable: {probeState.error}</span>
            )}
          </div>
        )}

        <div className="neural-panel__field">
          <label className="neural-panel__label">Input</label>
          <input
            className="neural-panel__input neural-panel__input--path"
            type="text"
            value={inputPath}
            placeholder="Input WAV path (or right-click an audio clip → Render with RAVE…)"
            onChange={(e) => setInputPath(e.target.value)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Output</label>
          <input
            className="neural-panel__input neural-panel__input--path"
            type="text"
            value={outputPath}
            onChange={(e) => setOutputPath(e.target.value)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Temp</label>
          <input
            className="neural-panel__slider"
            type="range"
            min={0}
            max={2}
            step={0.05}
            value={temperature}
            onChange={(e) => setTemperature(parseFloat(e.target.value))}
          />
          <input
            className="neural-panel__input neural-panel__input--num"
            type="number"
            min={0}
            max={2}
            step={0.05}
            value={temperature}
            onChange={(e) => setTemperature(parseFloat(e.target.value) || 0)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Seed</label>
          <input
            className="neural-panel__input neural-panel__input--num"
            type="number"
            value={seed}
            onChange={(e) => setSeed(parseInt(e.target.value, 10) || 0)}
          />
        </div>

        <div className="neural-panel__actions">
          <button
            className="neural-panel__btn neural-panel__btn--primary"
            onClick={startTransform}
            disabled={!canStart}
          >
            {phase === "running" ? "Rendering…" : "Start Transform"}
          </button>
          {phase === "running" && (
            <button className="neural-panel__btn" onClick={cancelJob}>
              Cancel
            </button>
          )}
        </div>

        {phase !== "idle" && (
          <div className="neural-panel__status" data-phase={phase}>
            <span className="neural-panel__phase">{phase}</span>
            {jobMessage && <span className="neural-panel__msg">{jobMessage}</span>}
          </div>
        )}
      </div>

      <div className="neural-panel__col neural-panel__training" aria-label="RAVE training">
        <div className="neural-panel__title">Train Model</div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Dataset</label>
          <input
            className="neural-panel__input neural-panel__input--path"
            type="text"
            value={datasetPath}
            placeholder="Dataset folder containing .wav files"
            onChange={(e) => setDatasetPath(e.target.value)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Model Out</label>
          <input
            className="neural-panel__input neural-panel__input--path"
            type="text"
            value={trainingOutputPath}
            onChange={(e) => setTrainingOutputPath(e.target.value)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Name</label>
          <input
            className="neural-panel__input neural-panel__input--path"
            type="text"
            value={trainingName}
            onChange={(e) => setTrainingName(e.target.value)}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">Epochs</label>
          <input
            className="neural-panel__input neural-panel__input--num"
            type="number"
            min={1}
            value={trainingEpochs}
            onChange={(e) => setTrainingEpochs(Math.max(1, parseInt(e.target.value, 10) || 1))}
          />
          <label className="neural-panel__label">Batch</label>
          <input
            className="neural-panel__input neural-panel__input--num"
            type="number"
            min={1}
            value={trainingBatchSize}
            onChange={(e) => setTrainingBatchSize(Math.max(1, parseInt(e.target.value, 10) || 1))}
          />
        </div>

        <div className="neural-panel__field">
          <label className="neural-panel__label">SR</label>
          <input
            className="neural-panel__input neural-panel__input--num"
            type="number"
            min={1}
            value={trainingSampleRate}
            onChange={(e) => setTrainingSampleRate(Math.max(1, parseInt(e.target.value, 10) || 1))}
          />
        </div>

        <div className="neural-panel__actions">
          <button
            className="neural-panel__btn neural-panel__btn--primary"
            onClick={startTraining}
            disabled={!canStartTraining}
          >
            {trainingPhase === "running" ? "Training…" : "Start Training"}
          </button>
          <button className="neural-panel__btn" onClick={refreshTrainingStatus} disabled={trainingJobIdRef.current == null}>
            Status
          </button>
          {trainingPhase === "running" && (
            <button className="neural-panel__btn" onClick={cancelTrainingJob}>
              Cancel Training
            </button>
          )}
        </div>

        {trainingPhase !== "idle" && (
          <div className="neural-panel__status" data-phase={trainingPhase}>
            <span className="neural-panel__phase">{trainingPhase}</span>
            {trainingMessage && <span className="neural-panel__msg">{trainingMessage}</span>}
          </div>
        )}
      </div>

      {phase === "finished" && (
        <div className="neural-panel__col neural-panel__import">
          <div className="neural-panel__title">Import Result</div>
          <div className="neural-panel__output" title={outputPath}>{outputPath}</div>

          <div className="neural-panel__field">
            <label className="neural-panel__label">Track</label>
            <input
              className="neural-panel__input neural-panel__input--num"
              type="number"
              min={0}
              value={importTrack}
              onChange={(e) => setImportTrack(parseInt(e.target.value, 10) || 0)}
            />
          </div>

          <div className="neural-panel__field">
            <label className="neural-panel__label">Start (beats)</label>
            <input
              className="neural-panel__input neural-panel__input--num"
              type="number"
              min={0}
              step={0.25}
              value={importStartBeats}
              onChange={(e) => setImportStartBeats(parseFloat(e.target.value) || 0)}
            />
          </div>

          <div className="neural-panel__field">
            <label className="neural-panel__label">
              <input
                type="checkbox"
                checked={alignToGrid}
                onChange={(e) => setAlignToGrid(e.target.checked)}
              />
              Align to grid
            </label>
          </div>

          <div className="neural-panel__field">
            <label className="neural-panel__label">Sampler</label>
            <input
              className="neural-panel__input neural-panel__input--num"
              type="number"
              min={-1}
              value={samplerTrack}
              title="Sampler track index (-1 = off)"
              onChange={(e) => setSamplerTrack(parseInt(e.target.value, 10))}
            />
            <input
              className="neural-panel__input neural-panel__input--num"
              type="number"
              min={-1}
              value={samplerSlot}
              title="Sampler FX slot index (-1 = off)"
              onChange={(e) => setSamplerSlot(parseInt(e.target.value, 10))}
            />
          </div>

          <div className="neural-panel__actions">
            <button
              className="neural-panel__btn neural-panel__btn--primary"
              onClick={importResult}
            >
              Import
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
