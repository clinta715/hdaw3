import React, { useState, useEffect, useCallback, useRef } from "react";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./SamplerEditor.css";

interface SamplerState {
  sampleFile: string;
  mode: string;
  rootNote: number;
  transpose: number;
  mono: boolean;
  playReverse: boolean;
  envelope: { attack: number; decay: number; sustain: number; release: number };
  hasSound: boolean;
  activeVoices: number;
}

const MODES = ["classic", "one-shot", "slice"] as const;

export default function SamplerEditor() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [state, setState] = useState<SamplerState | null>(null);
  const [slotIndex, setSlotIndex] = useState<number>(-1);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (selectedTrackIndex == null) {
      setState(null);
      setSlotIndex(-1);
      return;
    }
    rpc.call("read.getFxSlots", { trackIndex: selectedTrackIndex })
      .then((data) => {
        if (!Array.isArray(data)) { setState(null); setSlotIndex(-1); return; }
        const slots = data as { fxType: string }[];
        const idx = slots.findIndex((s) => s.fxType === "sampler");
        setSlotIndex(idx);
        if (idx >= 0) {
          return rpc.call("read.sampler.getState", {
            trackIndex: selectedTrackIndex,
            slotIndex: idx,
          });
        }
        setState(null);
        return null;
      })
      .then((data) => {
        if (data) setState(data as SamplerState);
      })
      .catch(() => setState(null));
  }, [selectedTrackIndex]);

  const refreshState = useCallback(() => {
    if (selectedTrackIndex == null || slotIndex < 0) return;
    rpc.call("read.sampler.getState", { trackIndex: selectedTrackIndex, slotIndex })
      .then((data) => setState(data as SamplerState))
      .catch(() => {});
  }, [selectedTrackIndex, slotIndex]);

  const setParam = useCallback(async (paramIndex: number, value: number) => {
    if (selectedTrackIndex == null || slotIndex < 0) return;
    await rpc.call("setFxSlotParam", {
      trackIndex: selectedTrackIndex,
      slotIndex,
      paramIndex,
      value,
    });
    refreshState();
  }, [selectedTrackIndex, slotIndex, refreshState]);

  const setMode = useCallback(async (mode: string) => {
    if (selectedTrackIndex == null || slotIndex < 0) return;
    const modeMap: Record<string, number> = { "classic": 0, "one-shot": 1, "slice": 2 };
    await rpc.call("setFxSlotParam", {
      trackIndex: selectedTrackIndex,
      slotIndex,
      paramIndex: 6,
      value: modeMap[mode] ?? 0,
    });
    refreshState();
  }, [selectedTrackIndex, slotIndex, refreshState]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !state?.hasSound) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    ctx.clearRect(0, 0, w, h);

    const accent = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#d97706";
    ctx.strokeStyle = accent;
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = 0; x < w; x++) {
      const y = h / 2 + Math.sin(x * 0.05) * (h * 0.3) * Math.random();
      if (x === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }, [state?.hasSound]);

  if (selectedTrackIndex == null || slotIndex < 0 || !state) {
    return (
      <div className="sampler-editor sampler-editor--empty">
        <span className="sampler-editor__hint">No sampler on this track</span>
      </div>
    );
  }

  const env = state.envelope;

  return (
    <div className="sampler-editor">
      <div className="sampler-editor__controls">
        <div className="sampler-editor__field">
          <label className="sampler-editor__label">Mode</label>
          <select
            className="sampler-editor__select"
            value={state.mode}
            onChange={(e) => setMode(e.target.value)}
          >
            {MODES.map((m) => (
              <option key={m} value={m}>{m}</option>
            ))}
          </select>
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">Root</label>
          <input
            className="sampler-editor__input"
            type="number"
            min={0}
            max={127}
            value={state.rootNote}
            onChange={(e) => {
              rpc.call("sampler.setSample", {
                trackIndex: selectedTrackIndex,
                slotIndex,
                filePath: state.sampleFile || "",
                rootNote: parseInt(e.target.value, 10),
              }).then(refreshState);
            }}
          />
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">Transpose</label>
          <input
            className="sampler-editor__input"
            type="number"
            min={-36}
            max={36}
            value={state.transpose}
            onChange={(e) => setParam(4, parseFloat(e.target.value))}
          />
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">
            <input
              type="checkbox"
              checked={state.mono}
              onChange={(e) => {
                rpc.call("setFxSlotParam", {
                  trackIndex: selectedTrackIndex,
                  slotIndex,
                  paramIndex: 7,
                  value: e.target.checked ? 1 : 0,
                }).then(refreshState);
              }}
            />
            Mono
          </label>
        </div>
      </div>

      <div className="sampler-editor__waveform">
        <canvas
          ref={canvasRef}
          className="sampler-editor__canvas"
        />
        {!state.hasSound && (
          <div className="sampler-editor__no-sample">No sample loaded</div>
        )}
      </div>

      <div className="sampler-editor__ahdsr">
        <div className="sampler-editor__field">
          <label className="sampler-editor__label">A</label>
          <input
            className="sampler-editor__slider"
            type="range"
            min={0}
            max={5}
            step={0.001}
            value={env.attack}
            onChange={(e) => setParam(0, parseFloat(e.target.value))}
          />
          <span className="sampler-editor__value">{env.attack.toFixed(3)}s</span>
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">D</label>
          <input
            className="sampler-editor__slider"
            type="range"
            min={0}
            max={5}
            step={0.001}
            value={env.decay}
            onChange={(e) => setParam(1, parseFloat(e.target.value))}
          />
          <span className="sampler-editor__value">{env.decay.toFixed(3)}s</span>
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">S</label>
          <input
            className="sampler-editor__slider"
            type="range"
            min={0}
            max={1}
            step={0.01}
            value={env.sustain}
            onChange={(e) => setParam(2, parseFloat(e.target.value))}
          />
          <span className="sampler-editor__value">{Math.round(env.sustain * 100)}%</span>
        </div>

        <div className="sampler-editor__field">
          <label className="sampler-editor__label">R</label>
          <input
            className="sampler-editor__slider"
            type="range"
            min={0}
            max={10}
            step={0.001}
            value={env.release}
            onChange={(e) => setParam(3, parseFloat(e.target.value))}
          />
          <span className="sampler-editor__value">{env.release.toFixed(3)}s</span>
        </div>

        <canvas
          className="sampler-editor__env-canvas"
          width={160}
          height={60}
          ref={(canvas) => {
            if (!canvas) return;
            const ctx = canvas.getContext("2d");
            if (!ctx) return;
            const w = canvas.width;
            const h = canvas.height;
            ctx.clearRect(0, 0, w, h);
            const total = env.attack + env.decay + 0.5 + env.release;
            if (total <= 0) return;
            const ax = (env.attack / total) * w;
            const dx = ax + (env.decay / total) * w;
            const sx = dx + (0.5 / total) * w;
            const accent = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#d97706";
            ctx.strokeStyle = accent;
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(0, h);
            ctx.lineTo(ax, 0);
            ctx.lineTo(dx, h * (1 - env.sustain));
            ctx.lineTo(sx, h * (1 - env.sustain));
            ctx.lineTo(w, h);
            ctx.stroke();
          }}
        />
      </div>
    </div>
  );
}
