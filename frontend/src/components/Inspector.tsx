import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import type { TrackSnapshot, ClipSnapshot } from "../rpc/types";
import "./Inspector.css";

const TRACK_TYPE_LABELS: Record<number, string> = { 0: "Audio", 1: "Instrument", 2: "Folder" };
const STRETCH_MODE_LABELS: Record<number, string> = { 0: "None", 1: "Resample", 2: "Beats", 3: "Tones", 4: "Texture", 5: "Poly" };

function TrackInspector({ track }: { track: TrackSnapshot }) {
  const handleName = (e: React.FocusEvent<HTMLInputElement> | React.KeyboardEvent<HTMLInputElement>) => {
    const val = (e.target as HTMLInputElement).value.trim();
    if (val && val !== track.name) {
      rpc.call("project.setTrackName", { trackIndex: track.index, name: val }).catch(console.error);
    }
  };

  const handleColor = () => {
    const input = document.createElement("input");
    input.type = "color";
    input.value = colorStr(track.color);
    input.addEventListener("input", () => {
      const hex = input.value.replace("#", "");
      const color = parseInt(hex, 16);
      rpc.call("project.setTrackColor", { trackIndex: track.index, color }).catch(console.error);
    });
    input.click();
  };

  const ToggleBtn = ({ label, value, rpcMethod, paramName }: { label: string; value: boolean; rpcMethod: string; paramName: string }) => (
    <button
      className={`insp-toggle${value ? " insp-toggle--active" : ""}`}
      onClick={() => rpc.call(rpcMethod, { trackIndex: track.index, [paramName]: !value }).catch(console.error)}
    >
      {label}
    </button>
  );

  const ReadOnly = ({ label, value }: { label: string; value: string }) => (
    <div className="insp-row">
      <span className="insp-label">{label}</span>
      <span className="insp-value insp-value--ro">{value}</span>
    </div>
  );

  return (
    <div className="insp-panel">
      <h3 className="insp-heading">Track {track.index}</h3>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Identity</legend>
        <div className="insp-row">
          <span className="insp-label">Name</span>
          <input
            type="text"
            className="insp-input"
            defaultValue={track.name}
            onBlur={handleName}
            onKeyDown={(e) => { if (e.key === "Enter") handleName(e); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Type</span>
          <select
            className="insp-input"
            value={track.trackType}
            onChange={(e) => {
              rpc.call("project.setTrackType", { trackIndex: track.index, trackType: Number(e.target.value) }).catch(console.error);
            }}
          >
            <option value={0}>Audio</option>
            <option value={1}>Instrument</option>
            <option value={2}>Folder</option>
          </select>
        </div>
        <div className="insp-row">
          <span className="insp-label">Color</span>
          <button className="insp-color-btn" style={{ background: colorStr(track.color) }} onClick={handleColor} title="Click to change track color" />
        </div>
        <ReadOnly label="Index" value={String(track.index)} />
        <div className="insp-row">
          <span className="insp-label">MIDI Ch</span>
          <input
            type="number"
            className="insp-input"
            min={0}
            max={15}
            defaultValue={track.midiChannel}
            onBlur={(e) => {
              const v = parseInt(e.target.value, 10);
              if (v >= 0 && v <= 15) {
                rpc.call("project.setTrackMidiChannel", { trackIndex: track.index, channel: v }).catch(console.error);
              }
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Mixer</legend>
        <div className="insp-row">
          <span className="insp-label">Volume</span>
          <input
            type="range"
            className="insp-slider"
            min="0"
            max="2"
            step="0.01"
            defaultValue={track.volume}
            onMouseUp={(e) => {
              rpc.call("project.setTrackVolume", { trackIndex: track.index, volume: Number((e.target as HTMLInputElement).value) }).catch(console.error);
            }}
          />
          <span className="insp-value">{Math.round(track.volume * 100)}%</span>
        </div>
        <div className="insp-row">
          <span className="insp-label">Pan</span>
          <input
            type="range"
            className="insp-slider"
            min="-1"
            max="1"
            step="0.01"
            defaultValue={track.pan}
            onMouseUp={(e) => {
              rpc.call("project.setTrackPan", { trackIndex: track.index, pan: Number((e.target as HTMLInputElement).value) }).catch(console.error);
            }}
          />
          <span className="insp-value">{track.pan === 0 ? "C" : `${Math.round(Math.abs(track.pan) * 100)}${track.pan < 0 ? "L" : "R"}`}</span>
        </div>
        <div className="insp-toggles">
          <ToggleBtn label="M" value={track.muted} rpcMethod="project.setTrackMuted" paramName="muted" />
          <ToggleBtn label="S" value={track.soloed} rpcMethod="project.setTrackSoloed" paramName="soloed" />
        </div>
        <ReadOnly label="Eff. Muted" value={track.effectiveMuted ? "Yes" : "No"} />
        <ReadOnly label="Eff. Soloed" value={track.effectiveSoloed ? "Yes" : "No"} />
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">I/O</legend>
        <div className="insp-toggles">
          <ToggleBtn label="Arm" value={track.armed} rpcMethod="project.setTrackArmed" paramName="armed" />
          <ToggleBtn label="Monitor" value={track.inputMonitor} rpcMethod="project.setTrackInputMonitor" paramName="monitor" />
          <ToggleBtn label="Hidden" value={track.isHidden ?? false} rpcMethod="project.setTrackHidden" paramName="hidden" />
        </div>
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Structure</legend>
        <div className="insp-row">
          <span className="insp-label">Collapsed</span>
          <ToggleBtn label={track.isCollapsed ? "Collapsed" : "Expanded"} value={track.isCollapsed ?? false} rpcMethod="project.setTrackCollapsed" paramName="collapsed" />
        </div>
        <ReadOnly label="Clip Count" value={String(track.clipCount)} />
        <ReadOnly label="Parent ID" value={track.parentId != null && track.parentId >= 0 ? String(track.parentId) : "none"} />
        <ReadOnly label="Child IDs" value={track.childIds && track.childIds.length > 0 ? track.childIds.join(", ") : "none"} />
      </fieldset>
    </div>
  );
}

function ClipInspector({ clip }: { clip: ClipSnapshot }) {
  const handleName = (e: React.FocusEvent<HTMLInputElement> | React.KeyboardEvent<HTMLInputElement>) => {
    const val = (e.target as HTMLInputElement).value.trim();
    if (val && val !== clip.name) {
      rpc.call("project.setClipName", { clipId: clip.clipId, name: val }).catch(console.error);
    }
  };

  const ToggleBtn = ({ label, value, rpcMethod, paramName }: { label: string; value: boolean; rpcMethod: string; paramName: string }) => (
    <button
      className={`insp-toggle${value ? " insp-toggle--active" : ""}`}
      onClick={() => rpc.call(rpcMethod, { clipId: clip.clipId, [paramName]: !value }).catch(console.error)}
    >
      {label}
    </button>
  );

  const ReadOnly = ({ label, value }: { label: string; value: string }) => (
    <div className="insp-row">
      <span className="insp-label">{label}</span>
      <span className="insp-value insp-value--ro">{value}</span>
    </div>
  );

  return (
    <div className="insp-panel">
      <h3 className="insp-heading">Clip {clip.clipId}</h3>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Identity</legend>
        <div className="insp-row">
          <span className="insp-label">Name</span>
          <input
            type="text"
            className="insp-input"
            defaultValue={clip.name}
            onBlur={handleName}
            onKeyDown={(e) => { if (e.key === "Enter") handleName(e); }}
          />
        </div>
        <ReadOnly label="Source" value={clip.sourceFile ? clip.sourceFile.split(/[\\/]/).pop() ?? clip.sourceFile : ""} />
        <ReadOnly label="Type" value={clip.isMidi ? "MIDI" : "Audio"} />
        <ReadOnly label="Clip ID" value={String(clip.clipId)} />
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Timing</legend>
        <div className="insp-row">
          <span className="insp-label">Start</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.startBeat}
            step="0.01"
            onBlur={(e) => {
              rpc.call("project.setClipStart", { clipId: clip.clipId, start: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Duration</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.durationBeats}
            step="0.01"
            onBlur={(e) => {
              rpc.call("project.setClipDuration", { clipId: clip.clipId, duration: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Offset</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.offset}
            step="0.01"
            onBlur={(e) => {
              rpc.call("project.setClipOffset", { clipId: clip.clipId, offset: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Source BPM</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.sourceBpm}
            step="0.1"
            onBlur={(e) => {
              rpc.call("project.setClipSourceBpm", { clipId: clip.clipId, bpm: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <ReadOnly label="Source Dur" value={String(clip.sourceDuration)} />
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Audio</legend>
        <div className="insp-row">
          <span className="insp-label">Gain</span>
          <input
            type="range"
            className="insp-slider"
            min="0"
            max="2"
            step="0.01"
            defaultValue={clip.gain}
            onMouseUp={(e) => {
              rpc.call("project.setClipGain", { clipId: clip.clipId, gain: Number((e.target as HTMLInputElement).value) }).catch(console.error);
            }}
          />
          <span className="insp-value">{Math.round(clip.gain * 100)}%</span>
        </div>
        <div className="insp-row">
          <span className="insp-label">Fade In</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.fadeIn}
            step="0.01"
            min="0"
            onBlur={(e) => {
              rpc.call("project.setClipFadeIn", { clipId: clip.clipId, fadeIn: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Fade Out</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.fadeOut}
            step="0.01"
            min="0"
            onBlur={(e) => {
              rpc.call("project.setClipFadeOut", { clipId: clip.clipId, fadeOut: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
        <div className="insp-row">
          <span className="insp-label">Stretch Mode</span>
          <select
            className="insp-input"
            value={clip.stretchMode}
            onChange={(e) => {
              rpc.call("project.setClipStretchMode", { clipId: clip.clipId, mode: Number(e.target.value) }).catch(console.error);
            }}
          >
            {Object.entries(STRETCH_MODE_LABELS).map(([k, v]) => (
              <option key={k} value={Number(k)}>{v}</option>
            ))}
          </select>
        </div>
        <div className="insp-row">
          <span className="insp-label">Stretch Ratio</span>
          <input
            type="number"
            className="insp-input"
            defaultValue={clip.stretchRatio}
            step="0.01"
            min="0"
            onBlur={(e) => {
              rpc.call("project.setClipStretchRatio", { clipId: clip.clipId, ratio: Number(e.target.value) }).catch(console.error);
            }}
            onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          />
        </div>
      </fieldset>

      <fieldset className="insp-group">
        <legend className="insp-group-legend">State</legend>
        <div className="insp-toggles">
          <ToggleBtn label="Loop" value={clip.looping} rpcMethod="project.setClipLooping" paramName="looping" />
          <ToggleBtn label="Muted" value={clip.muted} rpcMethod="project.setClipMuted" paramName="muted" />
        </div>
      </fieldset>

      {!clip.isMidi && clip.takeCount > 1 && (
        <fieldset className="insp-group">
          <legend className="insp-group-legend">Takes</legend>
          <div className="insp-takes">
            <div className="insp-take-selector">
              <button
                className="insp-take-btn"
                onClick={() => rpc.call("audioGraph.switchClipTakeToIndex", {
                  clipId: clip.clipId,
                  takeIndex: (clip.activeTake - 1 + clip.takeCount) % clip.takeCount,
                }).catch(console.error)}
                title="Previous take"
              >
                ◀
              </button>
              <select
                className="insp-take-select"
                value={clip.activeTake}
                onChange={(e) => rpc.call("audioGraph.switchClipTakeToIndex", {
                  clipId: clip.clipId,
                  takeIndex: parseInt(e.target.value, 10),
                }).catch(console.error)}
              >
                {clip.takes.map((t, i) => (
                  <option key={i} value={i}>{t.name || `Take ${i + 1}`}</option>
                ))}
              </select>
              <button
                className="insp-take-btn"
                onClick={() => rpc.call("audioGraph.switchClipTakeToIndex", {
                  clipId: clip.clipId,
                  takeIndex: (clip.activeTake + 1) % clip.takeCount,
                }).catch(console.error)}
                title="Next take"
              >
                ▶
              </button>
            </div>
          </div>
        </fieldset>
      )}

      <fieldset className="insp-group">
        <legend className="insp-group-legend">Structure</legend>
        <ReadOnly label="Track Index" value={String(clip.trackIndex)} />
        <ReadOnly label="Ghost" value={clip.isGhost ? "Yes" : "No"} />
        <ReadOnly label="Ghost Source" value={clip.isGhost ? String(clip.ghostSourceId) : "none"} />
        <ReadOnly label="Scene" value={clip.sceneIndex != null ? String(clip.sceneIndex) : "none"} />
        <ReadOnly label="Gain Envelope" value={clip.gainEnvelope.length > 0 ? `${clip.gainEnvelope.length} points` : "none"} />
      </fieldset>
    </div>
  );
}

export default function Inspector() {
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const snapshot = useProjectStore((s) => s.snapshot);

  const clipSelected = selectedClipIds.size === 1;

  if (clipSelected) {
    const clipId = selectedClipIds.values().next().value;
    const clip = snapshot?.clips.find((c) => c.clipId === clipId);
    if (!clip) {
      return <div className="insp-empty">Select a track or clip to inspect.</div>;
    }
    return <ClipInspector clip={clip} />;
  }

  if (selectedTrackIndex != null) {
    const track = snapshot?.tracks[selectedTrackIndex];
    if (!track) {
      return <div className="insp-empty">Select a track or clip to inspect.</div>;
    }
    return <TrackInspector track={track} />;
  }

  return <div className="insp-empty">Select a track or clip to inspect.</div>;
}