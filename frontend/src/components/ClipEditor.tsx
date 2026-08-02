import { useEffect, useState } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { quantizeWithGroove } from "./grooveUtils";
import GainEnvelopeEditor from "./GainEnvelopeEditor";
import "./ClipEditor.css";

const clamp = (v: number, min: number, max: number) => Math.max(min, Math.min(max, v));

export default function ClipEditor() {
  const clipId = useUiStore((s) => {
    const ids = s.selectedClipIds;
    return ids.size === 1 ? ids.values().next().value! : null;
  });
  const snapshot = useProjectStore((s) => s.snapshot);
  const notesByClip = useProjectStore((s) => s.notesByClip);
  const clip = clipId ? snapshot?.clips.find((c) => c.clipId === clipId) : null;

  const [transposeAmt, setTransposeAmt] = useState(12);
  const [quantStrength, setQuantStrength] = useState(100);
  const [velOffset, setVelOffset] = useState(0);

  // Load notes for MIDI clips so clip-level MIDI ops work.
  // Reads notesByClip from getState() to avoid a re-render cascade:
  // syncNotes creates a new Map → notesByClip changes → effect re-fires.
  useEffect(() => {
    if (clip?.isMidi && clipId != null) {
      const map = useProjectStore.getState().notesByClip;
      if (!map.has(clipId)) {
        useProjectStore.getState().syncNotes(rpc, clipId);
      }
    }
  }, [clip?.isMidi, clipId, rpc]);

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

  // ---- Clip-level MIDI operations (act on every note in the clip) ----
  const midiNotes = clip.isMidi && clipId != null ? (notesByClip.get(clipId) ?? []) : [];

  const transposeAll = async (semitones: number) => {
    if (clipId == null || semitones === 0) return;
    // Read fresh from the store to avoid stale-closure pitfalls
    const notes = useProjectStore.getState().notesByClip.get(clipId) ?? [];
    if (notes.length === 0) return;
    try {
      await rpc.call("project.beginTransaction", { name: `transpose ${semitones >= 0 ? "+" : ""}${semitones}` });
      for (const n of notes) {
        await rpc.call("project.setNotePitch", { noteId: n.noteId, pitch: clamp(n.pitch + semitones, 0, 127) });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip transpose failed", err);
    }
  };

  const quantizeAll = async () => {
    if (clipId == null) return;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    if (!snapEnabled) return;
    const notes = useProjectStore.getState().notesByClip.get(clipId) ?? [];
    if (notes.length === 0) return;
    const strength = quantStrength / 100;
    try {
      await rpc.call("project.beginTransaction", { name: "quantize clip" });
      for (const n of notes) {
        const newStart = quantizeWithGroove(n.startBeat, snapDivision, strength, 0);
        await rpc.call("project.setNoteStart", { noteId: n.noteId, startBeat: newStart });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip quantize failed", err);
    }
  };

  const applyVelocityOffset = async () => {
    if (clipId == null || velOffset === 0) return;
    const notes = useProjectStore.getState().notesByClip.get(clipId) ?? [];
    if (notes.length === 0) return;
    try {
      await rpc.call("project.beginTransaction", { name: `velocity ${velOffset >= 0 ? "+" : ""}${velOffset}` });
      for (const n of notes) {
        await rpc.call("project.setNoteVelocity", { noteId: n.noteId, velocity: clamp(n.velocity + velOffset, 1, 127) });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip velocity offset failed", err);
    }
  };

  const humanizeAll = async () => {
    if (clipId == null) return;
    const notes = useProjectStore.getState().notesByClip.get(clipId) ?? [];
    if (notes.length === 0) return;
    try {
      await rpc.call("project.beginTransaction", { name: "humanize clip" });
      for (const n of notes) {
        const beatOffset = (Math.random() - 0.5) * 0.06;
        const velOff = Math.round((Math.random() - 0.5) * 10);
        await rpc.call("project.setNoteStart", { noteId: n.noteId, startBeat: Math.max(0, n.startBeat + beatOffset) });
        await rpc.call("project.setNoteVelocity", { noteId: n.noteId, velocity: clamp(n.velocity + velOff, 1, 127) });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip humanize failed", err);
    }
  };

  const snapEnabled = useUiStore((s) => s.snapEnabled);

  return (
    <div className="clip-editor" key={clip.clipId}>
      <div className="ce-header">
        {clip.name ?? `Clip ${clip.clipId}`}
        {track ? <span className="ce-track"> — Track {track.name}</span> : null}
        {clip.isMidi ? <span className="ce-badge ce-midi">MIDI</span> : <span className="ce-badge ce-audio">Audio</span>}
      </div>

      {clip.isMidi ? (
        <div className="ce-body">
          <div className="ce-left">
            <div className="ce-section-title">MIDI — {midiNotes.length} note{midiNotes.length === 1 ? "" : "s"}</div>
            <div className="ce-row">
              <label>Gain</label>
              <input type="range" min={0} max={2} step={0.01} value={clip.gain}
                onChange={(e) => setGain(parseFloat(e.target.value))} />
              <span className="ce-val">{clip.gain.toFixed(2)}x</span>
            </div>
            <div className="ce-row">
              <label>Loop</label>
              <input type="checkbox" checked={clip.looping} onChange={toggleLoop} />
            </div>
            <div className="ce-row">
              <label>Transpose</label>
              <button className="ce-btn" onClick={() => transposeAll(-12)} title="Down an octave">−12</button>
              <button className="ce-btn" onClick={() => transposeAll(-1)} title="Down a semitone">−1</button>
              <button className="ce-btn" onClick={() => transposeAll(1)} title="Up a semitone">+1</button>
              <button className="ce-btn" onClick={() => transposeAll(12)} title="Up an octave">+12</button>
              <input type="number" min={-48} max={48} step={1} value={transposeAmt}
                onChange={(e) => setTransposeAmt(parseInt(e.target.value) || 0)} />
              <button className="ce-btn" onClick={() => transposeAll(transposeAmt)}>Apply</button>
            </div>
          </div>
          <div className="ce-right">
            <div className="ce-section-title">Process</div>
            <div className="ce-row">
              <label>Quantize</label>
              <input type="range" min={0} max={100} step={1} value={quantStrength}
                onChange={(e) => setQuantStrength(Number(e.target.value))} />
              <span className="ce-val">{quantStrength}%</span>
              <button className="ce-btn" onClick={quantizeAll} disabled={!snapEnabled}
                title={snapEnabled ? "Quantize all notes to snap grid" : "Enable snap first"}>
                Apply
              </button>
            </div>
            <div className="ce-row">
              <label>Velocity</label>
              <input type="range" min={-50} max={50} step={1} value={velOffset}
                onChange={(e) => setVelOffset(Number(e.target.value))} />
              <span className="ce-val">{velOffset >= 0 ? `+${velOffset}` : velOffset}</span>
              <button className="ce-btn" onClick={applyVelocityOffset} disabled={velOffset === 0}>Apply</button>
            </div>
            <div className="ce-row">
              <label>Humanize</label>
              <button className="ce-btn" onClick={humanizeAll}
                title="Add slight timing + velocity randomness to all notes">
                Humanize All
              </button>
            </div>
          </div>
        </div>
      ) : (
        <>
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

          <div className="ce-envelope">
            <div className="ce-section-title">Gain Envelope</div>
            <GainEnvelopeEditor
              clipId={clip.clipId}
              points={clip.gainEnvelope}
              durationBeats={dur}
            />
          </div>
        </>
      )}
    </div>
  );
}
