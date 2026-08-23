import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import type { ScaleModeInfo, ChordTypeInfo, ProgressionPatternInfo, StyleInfo, RhythmPatternResult } from "../rpc/types";
import PresetBrowser from "./PresetBrowser";
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
  const [seed, setSeed] = useState(0);
  const [trackIndex, setTrackIndex] = useState(selectedTrackIndex ?? 0);

  // Arrangement params
  const [bars, setBars] = useState(32);
  const [complexity, setComplexity] = useState(0.5);
  const [swing, setSwing] = useState(50);
  const [enKick, setEnKick] = useState(true);
  const [enHats, setEnHats] = useState(true);
  const [enClap, setEnClap] = useState(true);
  const [enBass, setEnBass] = useState(true);
  const [enLead, setEnLead] = useState(false);
  const [enChords, setEnChords] = useState(false);
  const [enSnare, setEnSnare] = useState(false);
  const [arrStyle, setArrStyle] = useState(0);

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

  // Rhythm params (mode 4)
  const [rhythmGrid, setRhythmGrid] = useState(16);
  const [rhythmBars, setRhythmBars] = useState(1);
  const [pulseA, setPulseA] = useState(4);
  const [pulseB, setPulseB] = useState(3);
  const [rotationA, setRotationA] = useState(1);
  const [rotationB, setRotationB] = useState(1);
  const [pitchA, setPitchA] = useState(36);
  const [pitchB, setPitchB] = useState(42);
  const [velocityA, setVelocityA] = useState(112);
  const [velocityB, setVelocityB] = useState(96);
  const [rhythmDsl, setRhythmDsl] = useState("");
  const [dslPitch, setDslPitch] = useState(39);

  const [styleParams, setStyleParams] = useState<Record<string, unknown>>({});
  const [styleParamSchema, setStyleParamSchema] = useState<Array<{
    name: string; type: string; min: number; max: number; default: number; label: string;
  }>>([]);

  const [preview, setPreview] = useState("");
  const [generating, setGenerating] = useState(false);

  // Analyze MIDI state
  const [analyzePath, setAnalyzePath] = useState("");
  const [analyzeResult, setAnalyzeResult] = useState<Record<string, unknown> | null>(null);
  const [analyzing, setAnalyzing] = useState(false);
  const [analyzeError, setAnalyzeError] = useState("");

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

  // Load style-specific parameter schema when phrase style changes
  useEffect(() => {
    if (mode !== 0) return;
    const styleName = styles[style]?.name;
    if (!styleName) return;
    rpc.call("composition.getStyleParams", { style: styleName })
      .then((result: unknown) => {
        if (result && typeof result === "object" && "fields" in (result as Record<string, unknown>)) {
          setStyleParamSchema((result as { fields: Array<{
            name: string; type: string; min: number; max: number; default: number; label: string;
          }> }).fields || []);
        }
      });
  }, [mode, style, styles]);

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
    else if (name === "Euclidean") { setLengthBeats(4); setDensity(5); }
    else                          { setLengthBeats(4); setDensity(8); }
  }, [styles]);

  const handleLoadPreset = useCallback((preset: {
    name: string; style: string;
    params: Record<string, unknown>;
    styleParams: Record<string, unknown>;
  }) => {
    const idx = styles.findIndex(s => s.name === preset.style);
    if (idx >= 0) setStyle(idx);
    const p = preset.params;
    if (p.scaleRoot !== undefined) setScaleRoot(p.scaleRoot as number);
    if (p.scaleMode !== undefined) setScaleMode(p.scaleMode as number);
    if (p.lowNote !== undefined) setLowNote(p.lowNote as number);
    if (p.highNote !== undefined) setHighNote(p.highNote as number);
    if (p.minVelocity !== undefined) setVelocity(p.minVelocity as number);
    if (p.seed !== undefined) setSeed(p.seed as number);
    if (p.lengthBeats !== undefined) setLengthBeats(p.lengthBeats as number);
    if (p.density !== undefined) setDensity(p.density as number);
    setStyleParams(preset.styleParams || {});
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
        seed,
      };

      if (mode === 0) {
        result = await rpc.call("composition.generatePhrase", {
          ...shared,
          style: styles[style]?.name ?? "Standard",
          lengthBeats,
          density,
          noteDuration: 0.5,
          styleParams,
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
      } else if (mode === 2) {
        result = await rpc.call("composition.generateProgression", {
          ...shared,
          patternIndex,
          chordTypeOverride,
          beatsPerChord,
          durationBeats: progDuration,
          arpeggiate,
          arpeggioRate,
        }) as { clipId: number; noteCount: number };
      } else if (mode === 3) {
        const arr = await rpc.call("composition.generateArrangement", {
          ...shared,
          bars,
          complexity,
          swingPercent: swing,
          style: arrStyle,
          enableKick: enKick,
          enableClosedHat: enHats,
          enableOpenHat: enHats,
          enableClap: enClap,
          enableSnare: enSnare,
          enableBass: enBass,
          enableLead: enLead,
          enableChords: enChords,
        }) as { trackIndices: number[]; clipIds: number[]; noteCount: number; seed: number };
        setPreview(`Arrangement: ${arr.noteCount} notes across ${arr.clipIds.length} clips`);
        useProjectStore.setState({ isDirty: true });
        setTimeout(() => onClose(), 400);
        return;
      } else if (mode === 4) {
        const result = await rpc.call("composition.generateRhythmPattern", {
          trackIndex,
          startBeat: 0,
          grid: rhythmGrid,
          bars: rhythmBars,
          pulseA,
          pulseB,
          rotationA,
          rotationB,
          pitchA,
          pitchB,
          velocityA,
          velocityB,
          dsl: rhythmDsl.trim(),
          dslPitch,
        }) as RhythmPatternResult;
        setPreview(`Rhythm: ${result.noteCount} notes`);
        useProjectStore.setState({ isDirty: true });
        setTimeout(() => onClose(), 400);
        return;
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

  const handleAnalyze = async () => {
    if (!analyzePath.trim()) return;
    setAnalyzing(true);
    setAnalyzeError("");
    setAnalyzeResult(null);
    try {
      const result = await rpc.call("composition.analyzeMidiFile", { path: analyzePath.trim() });
      setAnalyzeResult(result as Record<string, unknown>);
    } catch (err) {
      setAnalyzeError(String(err));
    } finally {
      setAnalyzing(false);
    }
  };

  const handleApplyAnalysis = () => {
    if (!analyzeResult) return;
    const fp = analyzeResult.fingerprint as Record<string, unknown> | undefined;
    if (fp) {
      if (typeof fp.rootNote === "number") setScaleRoot(fp.rootNote % 12);
      if (typeof fp.scaleType === "number") setScaleMode(fp.scaleType);
      if (typeof fp.lowNote === "number") setLowNote(fp.lowNote);
      if (typeof fp.highNote === "number") setHighNote(fp.highNote);
      if (typeof fp.avgVelocity === "number") setVelocity(Math.round(fp.avgVelocity * 127));
    }
    // Apply the guessed style
    const styleName = analyzeResult.guessedStyle as string;
    if (styleName) {
      const idx = styles.findIndex(s => s.name === styleName);
      if (idx >= 0) setStyle(idx);
    }
    setPreview("Analysis applied — switch to Phrase mode to generate");
  };

  const trackCount = snapshot?.tracks.length ?? 0;

  return (
    <div className="pgd-overlay" onClick={onClose}>
      <div className="pgd-dialog" onClick={(e) => e.stopPropagation()}>
        <div className="pgd-header">
          <h3>Phrase Generator</h3>
          <button className="pgd-close" onClick={onClose}>×</button>
        </div>

        <div className="pgd-container">
          <PresetBrowser onLoadPreset={handleLoadPreset} currentStyle={styles[style]?.name || ""} />
          <div className="pgd-content">
            {/* Mode selector */}
          <div className="pgd-row">
            <label className="pgd-label">Mode</label>
            <select className="pgd-select pgd-mode-select" aria-label="Mode" value={mode} onChange={(e) => setMode(Number(e.target.value))}>
              <option value={0}>Phrase</option>
              <option value={1}>Single Chord</option>
              <option value={2}>Chord Progression</option>
              <option value={3}>Arrangement</option>
              <option value={4}>Rhythm</option>
              <option value={5}>Analyze MIDI</option>
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

          <div className="pgd-row">
            <label className="pgd-label">Seed</label>
            <input
              className="pgd-input"
              type="number"
              min={0}
              value={seed}
              onChange={(e) => setSeed(Number(e.target.value))}
            />
            <span className="pgd-unit">{seed === 0 ? "random" : "fixed"}</span>
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
              {styleParamSchema.length > 0 && (
                <div className="pgd-style-params">
                  <h4>{styles[style]?.name} Parameters</h4>
                  {styleParamSchema.map((field) => (
                    <div key={field.name} className="pgd-param-row">
                      <label>{field.label}</label>
                      {field.type === "bool" ? (
                        <input
                          type="checkbox"
                          checked={(styleParams[field.name] as boolean) ?? (field.default === 1)}
                          onChange={(e) =>
                            setStyleParams((prev) => ({ ...prev, [field.name]: e.target.checked }))
                          }
                        />
                      ) : field.type === "int" ? (
                        <input
                          type="number"
                          min={field.min}
                          max={field.max}
                          value={(styleParams[field.name] as number) ?? field.default}
                          onChange={(e) =>
                            setStyleParams((prev) => ({ ...prev, [field.name]: parseInt(e.target.value) || field.default }))
                          }
                        />
                      ) : (
                        <input
                          type="range"
                          min={field.min}
                          max={field.max}
                          step={0.01}
                          value={(styleParams[field.name] as number) ?? field.default}
                          onChange={(e) =>
                            setStyleParams((prev) => ({ ...prev, [field.name]: parseFloat(e.target.value) }))
                          }
                        />
                      )}
                    </div>
                  ))}
                </div>
              )}
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

          {/* Arrangement page */}
          {mode === 3 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Bars</label>
                <input className="pgd-input" type="number" min={4} max={256} value={bars} onChange={(e) => setBars(Number(e.target.value))} />
                <span className="pgd-unit">bars</span>
              </div>
              <div className="pgd-row pgd-velocity-row">
                <label className="pgd-label">Complexity</label>
                <input className="pgd-slider" type="range" min={0} max={1} step={0.05} value={complexity} onChange={(e) => setComplexity(Number(e.target.value))} />
                <span className="pgd-value">{complexity.toFixed(2)}</span>
              </div>
              <div className="pgd-row pgd-velocity-row">
                <label className="pgd-label">Swing</label>
                <input className="pgd-slider" type="range" min={0} max={100} value={swing} onChange={(e) => setSwing(Number(e.target.value))} />
                <span className="pgd-value">{swing}%</span>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Tracks</label>
                <div className="pgd-inline-group">
                  <label className="pgd-label-sm"><input type="checkbox" checked={enKick} onChange={(e) => setEnKick(e.target.checked)} /> Kick</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enHats} onChange={(e) => setEnHats(e.target.checked)} /> Hats</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enClap} onChange={(e) => setEnClap(e.target.checked)} /> Clap</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enSnare} onChange={(e) => setEnSnare(e.target.checked)} /> Snare</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enBass} onChange={(e) => setEnBass(e.target.checked)} /> Bass</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enLead} onChange={(e) => setEnLead(e.target.checked)} /> Lead</label>
                  <label className="pgd-label-sm"><input type="checkbox" checked={enChords} onChange={(e) => setEnChords(e.target.checked)} /> Chords</label>
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Style</label>
                <select className="pgd-select" value={arrStyle} onChange={(e) => setArrStyle(Number(e.target.value))}>
                  <option value={0}>Techno</option>
                  <option value={1}>House</option>
                  <option value={2}>DnB</option>
                </select>
              </div>
            </div>
          )}

          {/* Rhythm page */}
          {mode === 4 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Grid</label>
                <select className="pgd-select" value={rhythmGrid} onChange={(e) => setRhythmGrid(Number(e.target.value))}>
                  <option value={8}>8th notes</option>
                  <option value={16}>16th notes</option>
                  <option value={32}>32nd notes</option>
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Bars</label>
                <input className="pgd-input" type="number" min={1} max={8} value={rhythmBars} onChange={(e) => setRhythmBars(Number(e.target.value))} />
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Pulse A hits</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={pulseA} onChange={(e) => setPulseA(Number(e.target.value))} />
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Rotate A</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={rotationA} onChange={(e) => setRotationA(Number(e.target.value))} />
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Pitch A</label>
                <select className="pgd-select pgd-note-select" value={pitchA} onChange={(e) => setPitchA(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Pulse B hits</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={pulseB} onChange={(e) => setPulseB(Number(e.target.value))} />
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Rotate B</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={rotationB} onChange={(e) => setRotationB(Number(e.target.value))} />
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Pitch B</label>
                <select className="pgd-select pgd-note-select" value={pitchB} onChange={(e) => setPitchB(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">DSL</label>
                <input className="pgd-input" type="text" value={rhythmDsl} placeholder='E.g. "E(3,8,1) [x-]x2"' onChange={(e) => setRhythmDsl(e.target.value)} />
              </div>
              <div className="pgd-row">
                <label className="pgd-label">DSL Pitch</label>
                <select className="pgd-select pgd-note-select" value={dslPitch} onChange={(e) => setDslPitch(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
            </div>
          )}

          {/* Analyze MIDI page */}
          {mode === 5 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">MIDI File Path</label>
                <input className="pgd-input pgd-input-wide" type="text"
                  value={analyzePath}
                  placeholder='E:\midi\file.mid'
                  onChange={(e) => setAnalyzePath(e.target.value)} />
              </div>
              <div className="pgd-row">
                <button className="pgd-btn pgd-btn-analyze" onClick={handleAnalyze} disabled={analyzing || !analyzePath.trim()}>
                  {analyzing ? "Analyzing..." : "Analyze"}
                </button>
              </div>
              {analyzeError && (
                <div className="pgd-row pgd-error">{analyzeError}</div>
              )}
              {analyzeResult && (() => {
                const fp = (analyzeResult.fingerprint ?? null) as Record<string, number> | null;
                const patterns = Array.isArray(analyzeResult.patterns) ? analyzeResult.patterns as Array<Record<string, unknown>> : [];
                return (
                  <div className="pgd-analyze-results">
                    <div className="pgd-analyze-section">
                      <h4>Analysis: {String(analyzeResult.fileName ?? "Unknown")}</h4>
                      <div className="pgd-analyze-grid">
                        <span>BPM: {String(analyzeResult.sourceBpm)}</span>
                        <span>Time: {String(analyzeResult.timeSignature)}</span>
                        <span>Tracks: {String(analyzeResult.trackCount)}</span>
                        <span>Style: {String(analyzeResult.guessedStyle)}</span>
                      </div>
                    </div>
                    {fp && (
                      <div className="pgd-analyze-section">
                        <h4>Fingerprint</h4>
                        <div className="pgd-analyze-grid">
                          <span>Density: {(fp.avgNoteDensity ?? 0).toFixed(1)} notes/bar</span>
                          <span>Duration: {(fp.avgNoteDuration ?? 0).toFixed(2)} beats</span>
                          <span>Velocity: {(fp.avgVelocity ?? 0).toFixed(2)}</span>
                          <span>Swing: {(fp.swingAmount ?? 0).toFixed(2)}</span>
                          <span>Syncopation: {(fp.syncopationScore ?? 0).toFixed(2)}</span>
                          <span>Complexity: {(fp.rhythmComplexity ?? 0).toFixed(2)}</span>
                          <span>Quantization: {(fp.quantizationStrength ?? 0).toFixed(2)}</span>
                          <span>Chromaticism: {(fp.chromaticism ?? 0).toFixed(2)}</span>
                          <span>Polyphony: {(fp.avgPolyphony ?? 0).toFixed(1)}</span>
                        </div>
                      </div>
                    )}
                    {patterns.length > 0 && (
                      <div className="pgd-analyze-section">
                        <h4>Patterns ({patterns.length})</h4>
                        <div className="pgd-analyze-list">
                          {patterns.slice(0, 5).map((p, i) => (
                            <span key={i} className="pgd-analyze-tag">
                              {String(p.name)} {(p.notes as unknown[])?.length || 0}n
                            </span>
                          ))}
                        </div>
                      </div>
                    )}
                    <div className="pgd-row">
                      <button className="pgd-btn pgd-btn-apply" onClick={handleApplyAnalysis}>
                        Apply to Generator
                      </button>
                    </div>
                  </div>
                );
              })()}
            </div>
          )}
          </div>
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
