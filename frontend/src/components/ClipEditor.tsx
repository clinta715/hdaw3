import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import GainEnvelopeEditor from "./GainEnvelopeEditor";
import "./ClipEditor.css";

export default function ClipEditor() {
  const clipId = useUiStore((s) => {
    const ids = s.selectedClipIds;
    return ids.size === 1 ? ids.values().next().value! : null;
  });
  const snapshot = useProjectStore((s) => s.snapshot);
  const clip = clipId ? snapshot?.clips.find((c) => c.clipId === clipId) : null;

  if (!clip) return null;
  const track = snapshot?.tracks.find((t) => t.index === clip.trackIndex);
  const dur = clip.durationBeats;

  const setGain = (v: number) => rpc.call("project.setClipGain", { clipId, gain: v }).catch(() => {});
  const setFadeIn = (v: number) => rpc.call("project.setClipFadeIn", { clipId, fadeIn: v }).catch(() => {});
  const setFadeOut = (v: number) => rpc.call("project.setClipFadeOut", { clipId, fadeOut: v }).catch(() => {});
  const toggleLoop = () => rpc.call("project.setClipLooping", { clipId, looping: !clip.looping }).catch(() => {});
  const setSrcBpm = (v: number) => rpc.call("project.setClipSourceBpm", { clipId, bpm: v }).catch(() => {});
  const setStretchMode = (v: number) => rpc.call("project.setClipStretchMode", { clipId, mode: v }).catch(() => {});
  const setStretchRatio = (v: number) => rpc.call("project.setClipStretchRatio", { clipId, ratio: v }).catch(() => {});

  return (
    <div className="clip-editor" key={clip.clipId}>
      <div className="ce-header">
        {clip.name ?? `Clip ${clip.clipId}`}
        {track ? <span className="ce-track"> — Track {track.name}</span> : null}
        {clip.isMidi ? <span className="ce-badge ce-midi">MIDI</span> : <span className="ce-badge ce-audio">Audio</span>}
      </div>

      <div className="ce-body">
        <div className="ce-left">
          <div className="ce-row">
            <label>Gain</label>
            <input type="range" min={0} max={2} step={0.01} value={clip.gain}
              onChange={(e) => setGain(parseFloat(e.target.value))} />
            <span className="ce-val">{clip.gain.toFixed(2)}x</span>
          </div>
          <div className="ce-row">
            <label>Fade In</label>
            <input type="range" min={0} max={dur / 2} step={0.1} value={clip.fadeIn}
              onChange={(e) => setFadeIn(parseFloat(e.target.value))} />
            <span className="ce-val">{clip.fadeIn.toFixed(1)}b</span>
          </div>
          <div className="ce-row">
            <label>Fade Out</label>
            <input type="range" min={0} max={dur / 2} step={0.1} value={clip.fadeOut}
              onChange={(e) => setFadeOut(parseFloat(e.target.value))} />
            <span className="ce-val">{clip.fadeOut.toFixed(1)}b</span>
          </div>
          <div className="ce-row">
            <label>Loop</label>
            <input type="checkbox" checked={clip.looping} onChange={toggleLoop} />
          </div>
        </div>

        <div className="ce-right">
          <div className="ce-section-title">Timestretch</div>
          <div className="ce-row">
            <label>Src BPM</label>
            <input type="number" min={0} max={400} step={0.1} defaultValue={clip.sourceBpm || ""}
              onBlur={(e) => { const v = parseFloat(e.target.value); if (!isNaN(v)) setSrcBpm(v); }} />
          </div>
          <div className="ce-row">
            <label>Mode</label>
            <select value={clip.stretchMode} onChange={(e) => setStretchMode(parseInt(e.target.value))}>
              <option value={0}>Off</option>
              <option value={1}>Tempo Match</option>
              <option value={2}>Manual Ratio</option>
            </select>
          </div>
          <div className="ce-row">
            <label>Ratio</label>
            <input type="range" min={0.25} max={4} step={0.01} value={clip.stretchRatio}
              disabled={clip.stretchMode !== 2}
              onChange={(e) => setStretchRatio(parseFloat(e.target.value))} />
            <span className="ce-val">{clip.stretchRatio.toFixed(2)}x</span>
          </div>
        </div>
      </div>

      {!clip.isMidi && (
        <div className="ce-envelope">
          <div className="ce-section-title">Gain Envelope</div>
          <GainEnvelopeEditor
            clipId={clip.clipId}
            points={clip.gainEnvelope}
            durationBeats={dur}
          />
        </div>
      )}
    </div>
  );
}
