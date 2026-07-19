import { useRef, useCallback, useEffect, useState } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useUiStore } from "../store/uiStore";
import { WaveformCanvas } from "./WaveformCanvas";
import { rpc } from "../rpc";
import "./AudioClipEditor.css";

function gainToDb(g: number): string {
  if (g <= 0) return "-inf";
  return (20 * Math.log10(g)).toFixed(1);
}

function dbToGain(db: number): number {
  return Math.pow(10, db / 20);
}

export default function AudioClipEditor() {
  const clipId = useUiStore((s) => {
    const ids = s.selectedClipIds;
    if (ids.size !== 1) return null;
    return ids.values().next().value ?? null;
  });

  const snapshot = useProjectStore((s) => s.snapshot);
  const transport = useTransportStore((s) => s.transport);
  const clip = clipId != null ? snapshot?.clips.find((c) => c.clipId === clipId) : null;
  const isAudio = clip && !clip.isMidi;

  const waveformRef = useRef<HTMLDivElement>(null);
  const [waveformWidth, setWaveformWidth] = useState(400);

  useEffect(() => {
    const el = waveformRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) {
        setWaveformWidth(Math.floor(entry.contentRect.width));
      }
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const call = useCallback((method: string, params: Record<string, unknown>) => {
    rpc.call(method, params).catch(() => {});
  }, []);

  if (!isAudio || !clip) {
    return (
      <div className="audio-clip-editor">
        <div className="ace-empty">Select a single audio clip to edit</div>
      </div>
    );
  }

  const track = snapshot?.tracks.find((t) => t.index === clip.trackIndex);
  const dur = clip.durationBeats;
  const maxFade = dur / 2;
  const playheadBeats = transport.currentTimeSeconds * (transport.bpm / 60);
  const clipRelBeats = playheadBeats - clip.startBeat;

  const handlePlay = () => call("transport.play", {});
  const handleStop = () => call("transport.stop", {});
  const handleZoomIn = () => { /* waveform auto-scales */ };
  const handleZoomOut = () => { /* waveform auto-scales */ };
  const handleClose = () => useUiStore.getState().clearSelection();

  const setGain = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipGain", { clipId, gain: v });
  };

  const setFadeIn = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipFadeIn", { clipId, fadeIn: v });
  };

  const setFadeOut = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipFadeOut", { clipId, fadeOut: v });
  };

  const toggleLoop = () => call("project.setClipLooping", { clipId, looping: !clip.looping });

  const setSrcBpm = (e: React.FocusEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipSourceBpm", { clipId, bpm: v });
  };

  const setStretchMode = (e: React.ChangeEvent<HTMLSelectElement>) => {
    call("project.setClipStretchMode", { clipId, mode: parseInt(e.target.value) });
  };

  const setStretchRatio = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipStretchRatio", { clipId, ratio: v });
  };

  const fitToLoop = () => call("project.fitClipToLoop", { clipId });

  const setOffset = (e: React.FocusEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipOffset", { clipId, offset: v });
  };

  const setDuration = (e: React.FocusEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) call("project.setClipDuration", { clipId, duration: v });
  };

  const sliceAtPlayhead = () => call("project.sliceClipAtPlayhead", { clipId });
  const sliceAtTransients = () => call("project.sliceClipAtTransients", { clipId });

  const gainDb = gainToDb(clip.gain);

  return (
    <div className="audio-clip-editor">
      {/* Header bar */}
      <div className="ace-header">
        <div className="ace-transport">
          <button onClick={handlePlay} title="Play">▶</button>
          <button onClick={handleStop} title="Stop">■</button>
        </div>
        <span className="ace-clip-name">{clip.name ?? `Clip ${clip.clipId}`}</span>
        {track && <span className="ace-track-name">— {track.name}</span>}
        <span className="ace-badge">Audio</span>
        <div className="ace-spacer" />
        <button className="ace-close" onClick={handleClose} title="Close">✕</button>
      </div>

      {/* Waveform display */}
      <div className="ace-waveform" ref={waveformRef}>
        <div className="ace-waveform-inner" style={{ width: waveformWidth }}>
          <WaveformCanvas clip={clip} width={waveformWidth} height={80} />
          {clipRelBeats >= 0 && clipRelBeats <= dur && (
            <div
              className="ace-playhead-overlay"
              style={{ left: (clipRelBeats / dur) * waveformWidth }}
            />
          )}
        </div>
      </div>

      {/* Controls */}
      <div className="ace-controls">
        <div className="ace-section">
          {/* Left column: Timestretch */}
          <div className="ace-section-col">
            <div className="ace-section-title">Timestretch</div>
            <div className="ace-row">
              <label>Src BPM</label>
              <input
                type="number"
                min={0}
                max={400}
                step={0.1}
                defaultValue={clip.sourceBpm || ""}
                onBlur={setSrcBpm}
              />
            </div>
            <div className="ace-row">
              <label>Mode</label>
              <select defaultValue={clip.stretchMode} onChange={setStretchMode}>
                <option value={0}>Off</option>
                <option value={1}>Tempo Match</option>
                <option value={2}>Manual Ratio</option>
              </select>
            </div>
            <div className="ace-row">
              <label>Ratio</label>
              <input
                type="range"
                min={0.25}
                max={4}
                step={0.01}
                defaultValue={clip.stretchRatio}
                disabled={clip.stretchMode !== 2}
                onChange={setStretchRatio}
              />
              <span className="ace-val">{clip.stretchRatio.toFixed(2)}x</span>
            </div>
            <div className="ace-row">
              <button className="ace-btn ace-btn--accent" onClick={fitToLoop}>
                Fit to Loop
              </button>
            </div>
          </div>

          {/* Middle column: Gain / Fades */}
          <div className="ace-section-col">
            <div className="ace-section-title">Levels</div>
            <div className="ace-row">
              <label>Gain</label>
              <input
                type="range"
                min={0}
                max={2}
                step={0.01}
                defaultValue={clip.gain}
                onChange={setGain}
              />
              <span className="ace-val">{gainDb} dB</span>
            </div>
            <div className="ace-row">
              <label>Fade In</label>
              <input
                type="range"
                min={0}
                max={maxFade}
                step={0.1}
                defaultValue={clip.fadeIn}
                onChange={setFadeIn}
              />
              <span className="ace-val">{clip.fadeIn.toFixed(1)}b</span>
            </div>
            <div className="ace-row">
              <label>Fade Out</label>
              <input
                type="range"
                min={0}
                max={maxFade}
                step={0.1}
                defaultValue={clip.fadeOut}
                onChange={setFadeOut}
              />
              <span className="ace-val">{clip.fadeOut.toFixed(1)}b</span>
            </div>
          </div>

          {/* Right column: Position / Loop */}
          <div className="ace-section-col">
            <div className="ace-section-title">Position</div>
            <div className="ace-row">
              <label>Loop</label>
              <input type="checkbox" checked={clip.looping} onChange={toggleLoop} />
            </div>
            <div className="ace-row">
              <label>Offset</label>
              <input
                type="number"
                min={0}
                step={0.1}
                defaultValue={clip.offset}
                onBlur={setOffset}
              />
              <span className="ace-val">b</span>
            </div>
            <div className="ace-row">
              <label>Duration</label>
              <input
                type="number"
                min={0.1}
                step={0.1}
                defaultValue={clip.durationBeats}
                onBlur={setDuration}
              />
              <span className="ace-val">b</span>
            </div>
          </div>
        </div>

        {/* Slice buttons */}
        <div className="ace-row">
          <button className="ace-btn" onClick={sliceAtPlayhead}>
            Slice @ Playhead
          </button>
          <button className="ace-btn" onClick={sliceAtTransients}>
            Slice @ Transients
          </button>
        </div>
      </div>
    </div>
  );
}
