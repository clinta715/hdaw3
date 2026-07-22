import { useRef, useCallback, useEffect, useState } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { useUiStore } from "../store/uiStore";
import { reportRpcError } from "../store/notifyStore";
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
  const [fileMissing, setFileMissing] = useState(false);

  const lastClipRef = useRef(clipId);
  useEffect(() => {
    setFileMissing(false);
    if (clip && clipId !== lastClipRef.current) {
      lastClipRef.current = clipId;
      setGainVal(clip.gain);
      setFadeInVal(clip.fadeIn);
      setFadeOutVal(clip.fadeOut);
      setStretchRatioVal(clip.stretchRatio);
    }
  }, [clipId, clip]);

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
    rpc.call(method, params).catch((err) => reportRpcError(method, err));
  }, []);

  // Local state for range inputs — optimistic update + commit on drag end
  const [gain, setGainVal] = useState(clip?.gain ?? 1);
  const gainRef = useRef(gain);
  gainRef.current = gain;
  const [fadeIn, setFadeInVal] = useState(clip?.fadeIn ?? 0);
  const fadeInRef = useRef(fadeIn);
  fadeInRef.current = fadeIn;
  const [fadeOut, setFadeOutVal] = useState(clip?.fadeOut ?? 0);
  const fadeOutRef = useRef(fadeOut);
  fadeOutRef.current = fadeOut;
  const [stretchRatio, setStretchRatioVal] = useState(clip?.stretchRatio ?? 1);
  const stretchRatioRef = useRef(stretchRatio);
  stretchRatioRef.current = stretchRatio;

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
    if (!isNaN(v)) setGainVal(v);
  };
  const commitGain = () => call("project.setClipGain", { clipId, gain: gainRef.current });

  const setFadeIn = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) setFadeInVal(v);
  };
  const commitFadeIn = () => call("project.setClipFadeIn", { clipId, fadeIn: fadeInRef.current });

  const setFadeOut = (e: React.ChangeEvent<HTMLInputElement>) => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) setFadeOutVal(v);
  };
  const commitFadeOut = () => call("project.setClipFadeOut", { clipId, fadeOut: fadeOutRef.current });

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
    if (!isNaN(v)) setStretchRatioVal(v);
  };
  const commitStretchRatio = () => call("project.setClipStretchRatio", { clipId, ratio: stretchRatioRef.current });

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

  const gainDb = gainToDb(gain);

  return (
    <div className="audio-clip-editor" key={clip.clipId}>
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
        {fileMissing ? (
          <div style={{ display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center", height: 80, gap: 4 }}>
            <span style={{ fontSize: 13, color: "#e05555", fontWeight: 600 }}>Source file not found</span>
            <span style={{ fontSize: 11, color: "#999" }}>{clip.sourceFile || "unknown path"}</span>
          </div>
        ) : (
          <div className="ace-waveform-inner" style={{ width: waveformWidth }}>
            <WaveformCanvas clip={clip} width={waveformWidth} height={80} onError={setFileMissing} />
            {clipRelBeats >= 0 && clipRelBeats <= dur && (
              <div
                className="ace-playhead-overlay"
                style={{ left: (clipRelBeats / dur) * waveformWidth }}
              />
            )}
          </div>
        )}
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
                value={stretchRatio}
                disabled={clip.stretchMode !== 2}
                onChange={setStretchRatio}
                onMouseUp={commitStretchRatio}
                onBlur={commitStretchRatio}
              />
              <span className="ace-val">{stretchRatio.toFixed(2)}x</span>
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
                value={gain}
                onChange={setGain}
                onMouseUp={commitGain}
                onBlur={commitGain}
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
                value={fadeIn}
                onChange={setFadeIn}
                onMouseUp={commitFadeIn}
                onBlur={commitFadeIn}
              />
              <span className="ace-val">{fadeIn.toFixed(1)}b</span>
            </div>
            <div className="ace-row">
              <label>Fade Out</label>
              <input
                type="range"
                min={0}
                max={maxFade}
                step={0.1}
                value={fadeOut}
                onChange={setFadeOut}
                onMouseUp={commitFadeOut}
                onBlur={commitFadeOut}
              />
              <span className="ace-val">{fadeOut.toFixed(1)}b</span>
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
