import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import type { ScaleModeInfo, ChordTypeInfo, ProgressionPatternInfo, StyleInfo } from "../rpc/types";
import "./PhraseGeneratorDialog.css";

const NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];

interface Props {
  onClose: () => void;
}

export default function PhraseGeneratorDialog({ onClose }: Props) {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);

  // Metadata (loaded once on open)
  const [scaleModes, setScaleModes] = useState<ScaleModeInfo[]>([]);
  const [chordTypes, setChordTypes] = useState<ChordTypeInfo[]>([]);
  const [patterns, setPatterns] = useState<ProgressionPatternInfo[]>([]);
  const [styles, setStyles] = useState<StyleInfo[]>([]);

  // Mode: 0=Phrase, 1=Single Chord, 2=Chord Progression
  const [mode, setMode] = useState(0);

  // Shared params — track index follows the current selection
  const [scaleRoot, setScaleRoot] = useState(snapshot?.scaleRoot ?? 0);
  const [scaleMode, setScaleMode] = useState(snapshot?.scaleMode ?? 0);
  const [lowNote, setLowNote] = useState(48);
  const [highNote, setHighNote] = useState(84);
  const [velocity, setVelocity] = useState(90);
  const [trackIndex, setTrackIndex] = useState(selectedTrackIndex ?? 0);

  // Keep track index in sync with the current selection
  useEffect(() => {
    if (selectedTrackIndex != null) setTrackIndex(selectedTrackIndex);
  }, [selectedTrackIndex]);

  // Phrase params
  const [style, setStyle] = useState(0);
  const [lengthBeats, setLengthBeats] = useState(4);
  const [density, setDensity] = useState(8);

  // Chord params
  const [chordType, setChordType] = useState(0);
  const [voicing, setVoicing] = useState(0);
  const [inversion, setInversion] = useState(0);
  const [arpeggiate, setArpeggiate] = useState(false);
  const [arpeggioRate, setArpeggioRate] = useState(0.125);
  const [chordDuration, setChordDuration] = useState(2.0);
  const [rootPitch, setRootPitch] = useState(60);

  // Progression params
  const [patternIndex, setPatternIndex] = useState(0);
  const [chordTypeOverride, setChordTypeOverride] = useState(-1);
  const [beatsPerChord, setBeatsPerChord] = useState(4.0);
  const [progDuration, setProgDuration] = useState(2.0);

  const [preview, setPreview] = useState("");
  const [generating, setGenerating] = useState(false);

  // Load metadata on mount
  useEffect(() => {
    Promise.all([
      rpc.call("composition.getScaleModes") as Promise<ScaleModeInfo[]>,
      rpc.call("composition.getChordTypes") as Promise<ChordTypeInfo[]>,
      rpc.call("composition.getProgressionPatterns") as Promise<ProgressionPatternInfo[]>,
      rpc.call("composition.getStyleNames") as Promise<StyleInfo[]>,
    ]).then(([sm, ct, pp, st]) => {
      setScaleModes(sm);
      setChordTypes(ct);
      setPatterns(pp);
      setStyles(st);
    }).catch(console.error);
  }, []);

  // Sync root/mode from project when snapshot changes
  useEffect(() => {
    if (snapshot) {
      setScaleRoot(snapshot.scaleRoot);
      setScaleMode(snapshot.scaleMode);
    }
  }, [snapshot?.scaleRoot, snapshot?.scaleMode]);

  // Smart defaults when style changes
  const handleStyleChange = useCallback((newStyle: number) => {
    setStyle(newStyle);
    const name = styles[newStyle]?.name ?? "";
    if (name === "Arpeggio")      { setLengthBeats(4); setDensity(16); }
    else if (name === "BassLine") { setLengthBeats(8); setDensity(16); setLowNote(24); setHighNote(60); }
    else if (name === "ChordStab") { setLengthBeats(2); setDensity(3); }
    else if (name === "Pad")      { setLengthBeats(8); setDensity(6); }
    else if (name === "Lead")     { setLengthBeats(4); setDensity(16); setLowNote(60); setHighNote(96); }
    else if (name === "RandomWalk") { setLengthBeats(4); setDensity(12); }
    else if (name === "Buildup")  { setLengthBeats(8); setDensity(32); }
    else                          { setLengthBeats(4); setDensity(8); }
  }, [styles]);

  const handleGenerate = async () => {
    if (generating) return;
    setGenerating(true);
    setPreview("");
    try {
      let result: { clipId: number; noteCount: number } | null = null;
      const shared = {
        trackIndex,
        scaleRoot,
        scaleMode,
        lowNote,
        highNote,
        minVelocity: Math.max(0, velocity - 20),
        maxVelocity: Math.min(127, velocity + 10),
      };

      if (mode === 0) {
        result = await rpc.call("composition.generatePhrase", {
          ...shared,
          style: styles[style]?.name ?? "Standard",
          lengthBeats,
          density,
          noteDuration: 0.5,
        }) as { clipId: number; noteCount: number };
      } else if (mode === 1) {
        result = await rpc.call("composition.generateChord", {
          ...shared,
          rootPitch,
          chordType,
          voicing,
          inversion,
          arpeggiate,
          arpeggioRate,
          durationBeats: chordDuration,
        }) as { clipId: number; noteCount: number };
      } else {
        result = await rpc.call("composition.generateProgression", {
          ...shared,
          patternIndex,
          chordTypeOverride,
          beatsPerChord,
          durationBeats: progDuration,
          arpeggiate,
          arpeggioRate,
        }) as { clipId: number; noteCount: number };
      }

      if (result) {
        setPreview(`Generated ${result.noteCount} notes`);
        // New clip is reconciled by the debounced notify.treeChanged push.
        useProjectStore.setState({ isDirty: true });
        setTimeout(() => onClose(), 400);
      }
    } catch (err) {
      setPreview("Error: " + String(err));
    } finally {
      setGenerating(false);
    }
  };

  const trackCount = snapshot?.tracks.length ?? 0;

  return (
    <div className="pgd-overlay" onClick={onClose}>
      <div className="pgd-dialog" onClick={(e) => e.stopPropagation()}>
        <div className="pgd-header">
          <h3>Phrase Generator</h3>
          <button className="pgd-close" onClick={onClose}>×</button>
        </div>

        <div className="pgd-body">
          {/* Mode selector */}
          <div className="pgd-row">
            <label className="pgd-label">Mode</label>
            <select className="pgd-select pgd-mode-select" value={mode} onChange={(e) => setMode(Number(e.target.value))}>
              <option value={0}>Phrase</option>
              <option value={1}>Single Chord</option>
              <option value={2}>Chord Progression</option>
            </select>
          </div>

          {/* Shared controls */}
          <div className="pgd-row">
            <label className="pgd-label">Track</label>
            <select className="pgd-select" value={trackIndex} onChange={(e) => setTrackIndex(Number(e.target.value))}>
              {snapshot?.tracks.map((t, i) => (
                <option key={i} value={i}>{t.name}</option>
              ))}
              {trackCount === 0 && <option value={0}>Track 1</option>}
            </select>
          </div>

          <div className="pgd-row-group">
            <div className="pgd-row">
              <label className="pgd-label">Root</label>
              <select className="pgd-select pgd-note-select" value={scaleRoot} onChange={(e) => setScaleRoot(Number(e.target.value))}>
                {NOTE_NAMES.map((n, i) => <option key={i} value={i}>{n}</option>)}
              </select>
            </div>
            <div className="pgd-row">
              <label className="pgd-label">Scale</label>
              <select className="pgd-select" value={scaleMode} onChange={(e) => setScaleMode(Number(e.target.value))}>
                {scaleModes.map((sm) => <option key={sm.index} value={sm.index}>{sm.name}</option>)}
              </select>
            </div>
          </div>

          <div className="pgd-row-group">
            <div className="pgd-row">
              <label className="pgd-label">Low</label>
              <select className="pgd-select pgd-note-select" value={lowNote} onChange={(e) => setLowNote(Number(e.target.value))}>
                {Array.from({ length: 85 }, (_, i) => i + 24).map((n) => (
                  <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1}</option>
                ))}
              </select>
            </div>
            <div className="pgd-row">
              <label className="pgd-label">High</label>
              <select className="pgd-select pgd-note-select" value={highNote} onChange={(e) => setHighNote(Number(e.target.value))}>
                {Array.from({ length: 85 }, (_, i) => i + 24).map((n) => (
                  <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1}</option>
                ))}
              </select>
            </div>
          </div>

          <div className="pgd-row pgd-velocity-row">
            <label className="pgd-label">Velocity</label>
            <input
              className="pgd-slider"
              type="range"
              min={30}
              max={127}
              value={velocity}
              onChange={(e) => setVelocity(Number(e.target.value))}
            />
            <span className="pgd-value">{velocity}</span>
          </div>

          {/* Phrase page */}
          {mode === 0 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Style</label>
                <select className="pgd-select" value={style} onChange={(e) => handleStyleChange(Number(e.target.value))}>
                  {styles.map((s) => <option key={s.index} value={s.index}>{s.name}</option>)}
                </select>
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Length</label>
                  <input className="pgd-input" type="number" min={1} max={64} value={lengthBeats} onChange={(e) => setLengthBeats(Number(e.target.value))} />
                  <span className="pgd-unit">beats</span>
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Density</label>
                  <input className="pgd-input" type="number" min={1} max={128} value={density} onChange={(e) => setDensity(Number(e.target.value))} />
                  <span className="pgd-unit">notes</span>
                </div>
              </div>
            </div>
          )}

          {/* Chord page */}
          {mode === 1 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Root Pitch</label>
                <select className="pgd-select pgd-note-select" value={rootPitch} onChange={(e) => setRootPitch(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Chord Type</label>
                <select className="pgd-select" value={chordType} onChange={(e) => setChordType(Number(e.target.value))}>
                  {chordTypes.map((ct) => <option key={ct.index} value={ct.index}>{ct.name}</option>)}
                </select>
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Voicing</label>
                  <select className="pgd-select" value={voicing} onChange={(e) => setVoicing(Number(e.target.value))}>
                    <option value={0}>Close</option>
                    <option value={1}>Open</option>
                    <option value={2}>Spread</option>
                  </select>
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Inversion</label>
                  <select className="pgd-select" value={inversion} onChange={(e) => setInversion(Number(e.target.value))}>
                    <option value={0}>Root</option>
                    <option value={1}>1st</option>
                    <option value={2}>2nd</option>
                    <option value={3}>3rd</option>
                  </select>
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">
                  <input type="checkbox" checked={arpeggiate} onChange={(e) => setArpeggiate(e.target.checked)} />
                  Arpeggiate
                </label>
                {arpeggiate && (
                  <div className="pgd-inline-group">
                    <label className="pgd-label-sm">Rate</label>
                    <input className="pgd-input pgd-input-sm" type="number" min={0.03125} max={2} step={0.03125} value={arpeggioRate} onChange={(e) => setArpeggioRate(Number(e.target.value))} />
                  </div>
                )}
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Duration</label>
                <input className="pgd-input" type="number" min={0.25} max={16} step={0.25} value={chordDuration} onChange={(e) => setChordDuration(Number(e.target.value))} />
                <span className="pgd-unit">beats</span>
              </div>
            </div>
          )}

          {/* Progression page */}
          {mode === 2 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Pattern</label>
                <select className="pgd-select" value={patternIndex} onChange={(e) => setPatternIndex(Number(e.target.value))}>
                  {patterns.map((p) => <option key={p.index} value={p.index}>{p.name}</option>)}
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Chord Override</label>
                <select className="pgd-select" value={chordTypeOverride} onChange={(e) => setChordTypeOverride(Number(e.target.value))}>
                  <option value={-1}>Default per degree</option>
                  {chordTypes.map((ct) => <option key={ct.index} value={ct.index}>{ct.name}</option>)}
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Beats/Chord</label>
                <input className="pgd-input" type="number" min={0.5} max={32} step={0.5} value={beatsPerChord} onChange={(e) => setBeatsPerChord(Number(e.target.value))} />
                <span className="pgd-unit">beats</span>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">
                  <input type="checkbox" checked={arpeggiate} onChange={(e) => setArpeggiate(e.target.checked)} />
                  Arpeggiate
                </label>
                {arpeggiate && (
                  <div className="pgd-inline-group">
                    <label className="pgd-label-sm">Rate</label>
                    <input className="pgd-input pgd-input-sm" type="number" min={0.03125} max={2} step={0.03125} value={arpeggioRate} onChange={(e) => setArpeggioRate(Number(e.target.value))} />
                  </div>
                )}
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Note Length</label>
                <input className="pgd-input" type="number" min={0.25} max={16} step={0.25} value={progDuration} onChange={(e) => setProgDuration(Number(e.target.value))} />
                <span className="pgd-unit">beats</span>
              </div>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="pgd-footer">
          {preview && <span className="pgd-preview">{preview}</span>}
          <div className="pgd-footer-btns">
            <button className="pgd-btn pgd-btn-cancel" onClick={onClose}>Cancel</button>
            <button className="pgd-btn pgd-btn-generate" onClick={handleGenerate} disabled={generating}>
              {generating ? "Generating..." : "Generate"}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
