import { useState, useMemo, useRef, useCallback, useEffect } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { quantizeWithGroove } from "./grooveUtils";
import NoteGrid from "./NoteGrid";
import VelocityLane from "./VelocityLane";
import CCLane from "./CCLane";
import NoteOperatorsPane from "./NoteOperatorsPane";
import "./PianoRoll.css";

const clamp = (v: number, min: number, max: number) => Math.max(min, Math.min(max, v));

export default function PianoRoll() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const notesByClip = useProjectStore((s) => s.notesByClip);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const [internalClipId, setInternalClipId] = useState<number | null>(null);
  const keysRef = useRef<HTMLDivElement>(null);
  const [selectedNoteIds, setSelectedNoteIds] = useState<Set<number>>(new Set());
  const [gridScrollLeft, setGridScrollLeft] = useState(0);
  const [ccLanes, setCcLanes] = useState<number[]>([]);
  const [ccToAdd, setCcToAdd] = useState(7);
  const [ccOpen, setCcOpen] = useState(false);
  const [editOpen, setEditOpen] = useState(false);
  const [velOpen, setVelOpen] = useState(false);
  const ccWrapRef = useRef<HTMLDivElement>(null);
  const editWrapRef = useRef<HTMLDivElement>(null);
  const [chordEnabled, setChordEnabled] = useState(false);
  const [chordType, setChordType] = useState("major");
  const [pixelsPerBeat, setPixelsPerBeat] = useState(80);
  const [quantizeStrength, setQuantizeStrength] = useState(100);
  const [swing, setSwing] = useState(0);
  const [clipOpen, setClipOpen] = useState(false);
  const clipWrapRef = useRef<HTMLDivElement>(null);
  const [clipGain, setClipGain] = useState(1);
  const [transposeAmt, setTransposeAmt] = useState(12);
  const [clipQuantStrength, setClipQuantStrength] = useState(100);
  const [velOffset, setVelOffset] = useState(0);

  const CHORD_SHAPES: Record<string, number[]> = {
    major: [0, 4, 7],
    minor: [0, 3, 7],
    diminished: [0, 3, 6],
    augmented: [0, 4, 8],
    maj7: [0, 4, 7, 11],
    min7: [0, 3, 7, 10],
    dom7: [0, 4, 7, 10],
    sus2: [0, 2, 7],
    sus4: [0, 5, 7],
  };

  const midiClips = snapshot?.clips.filter((c) => c.isMidi) ?? [];

  // Prefer the timeline-selected MIDI clip; fall back to internal selection, then first clip
  const timelineSelectedId = (() => {
    if (selectedClipIds.size !== 1) return null;
    const id = selectedClipIds.values().next().value!;
    const clip = snapshot?.clips.find((c) => c.clipId === id);
    return clip?.isMidi ? id : null;
  })();

  const selectedClipId = timelineSelectedId ?? internalClipId;

  const activeClip = selectedClipId != null
    ? midiClips.find((c) => c.clipId === selectedClipId)
    : midiClips[0];

  // Auto-load notes when a new clip becomes active.
  // Reads notesByClip from getState() to avoid a re-render cascade:
  // syncNotes creates a new Map → notesByClip changes → effect re-fires.
  useEffect(() => {
    if (activeClip) {
      const map = useProjectStore.getState().notesByClip;
      if (!map.has(activeClip.clipId)) {
        useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
      }
    }
  }, [activeClip?.clipId, rpc]);

  useEffect(() => {
    if (!ccOpen && !editOpen && !clipOpen) return;
    const onDown = (e: MouseEvent) => {
      const t = e.target as Node;
      if (ccOpen && ccWrapRef.current && !ccWrapRef.current.contains(t)) setCcOpen(false);
      if (editOpen && editWrapRef.current && !editWrapRef.current.contains(t)) setEditOpen(false);
      if (clipOpen && clipWrapRef.current && !clipWrapRef.current.contains(t)) setClipOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        setCcOpen(false);
        setEditOpen(false);
        setClipOpen(false);
      }
    };
    window.addEventListener("mousedown", onDown);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("mousedown", onDown);
      window.removeEventListener("keydown", onKey);
    };
  }, [ccOpen, editOpen, clipOpen]);

  useEffect(() => {
    setCcOpen(false);
    setEditOpen(false);
    setClipOpen(false);
  }, [activeClip?.clipId]);

  // Sync local clip gain state when active clip changes
  useEffect(() => {
    if (activeClip) {
      setClipGain(activeClip.gain);
    }
  }, [activeClip?.clipId, activeClip?.gain]);

  useEffect(() => {
    if (selectedNoteIds.size === 0) setEditOpen(false);
  }, [selectedNoteIds]);

  const notes = activeClip ? notesByClip.get(activeClip.clipId) ?? [] : [];
  const gridWidth = useMemo(() => {
    if (!notes.length) return 800;
    let maxEnd = 0;
    for (const n of notes) {
      const end = n.startBeat + n.durationBeats;
      if (end > maxEnd) maxEnd = end;
    }
    return Math.max(800, Math.ceil(maxEnd * pixelsPerBeat) + 200);
  }, [notes, pixelsPerBeat]);

  const loadNotes = (clipId: number) => {
    setInternalClipId(clipId);
    if (!notesByClip.has(clipId)) {
      useProjectStore.getState().syncNotes(rpc, clipId);
    }
  };

  const handleGridScroll = useCallback((scrollTop: number) => {
    if (keysRef.current) {
      keysRef.current.scrollTop = scrollTop;
    }
  }, []);

  // Called by NoteGrid's Ctrl+wheel zoom. Adjusts scroll so the beat under the
  // cursor stays fixed on screen.
  const handleZoom = useCallback(
    (newPpb: number, anchorLeft: number) => {
      setPixelsPerBeat((oldPpb) => {
        const gridEl = document.querySelector(".pr-grid-area .note-grid") as HTMLElement | null;
        if (!gridEl) return newPpb;
        const beat = (gridEl.scrollLeft + anchorLeft) / oldPpb;
        const newScrollLeft = Math.max(0, beat * newPpb - anchorLeft);
        requestAnimationFrame(() => { gridEl.scrollLeft = newScrollLeft; });
        return newPpb;
      });
    },
    []
  );

  // Button-driven zoom (clamped to the same range as Ctrl+wheel).
  const zoomBy = useCallback((factor: number) => {
    setPixelsPerBeat((p) => Math.round(Math.min(400, Math.max(20, p * factor))));
  }, []);

  const zoomFit = useCallback(() => {
    const el = document.querySelector(".pr-grid-area") as HTMLElement | null;
    const cw = el?.clientWidth ?? 800;
    const dur = activeClip?.durationBeats ?? 0;
    if (dur > 0) {
      setPixelsPerBeat(Math.round(Math.min(400, Math.max(20, cw / dur))));
    }
  }, [activeClip]);

  const handleVelocityChange = useCallback(
    async (noteId: number, velocity: number) => {
      try {
        await rpc.call("project.setNoteVelocity", { noteId, velocity });
        if (activeClip) {
          useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
        }
      } catch (err) {
        console.warn("velocity change failed", err);
      }
    },
    [activeClip]
  );

  const handleToggleLooping = useCallback(async () => {
    if (!activeClip) return;
    await rpc.call("project.setClipLooping", {
      clipId: activeClip.clipId,
      looping: !activeClip.looping,
    }).catch((err) => console.warn("loop toggle failed", err));
    useProjectStore.setState({ isDirty: true });
  }, [activeClip]);

  const handleScaleVelocity = useCallback(
    async (factor: number) => {
      if (!activeClip || selectedNoteIds.size === 0) return;
      const notesArr = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
      try {
        await rpc.call("project.beginTransaction", { name: "scale velocity" });
        for (const noteId of selectedNoteIds) {
          const note = notesArr.find((n) => n.noteId === noteId);
          if (!note) continue;
          const newVel = Math.max(1, Math.min(127, Math.round(note.velocity * factor)));
          await rpc.call("project.setNoteVelocity", { noteId, velocity: newVel });
        }
        await rpc.call("project.endTransaction");
        useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
        useProjectStore.setState({ isDirty: true });
      } catch (err) {
        console.warn("scale velocity failed", err);
      }
    },
    [activeClip, selectedNoteIds]
  );

  const handleScaleDuration = useCallback(
    async (factor: number) => {
      if (!activeClip || selectedNoteIds.size === 0) return;
      const notesArr = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
      try {
        await rpc.call("project.beginTransaction", { name: "scale duration" });
        for (const noteId of selectedNoteIds) {
          const note = notesArr.find((n) => n.noteId === noteId);
          if (!note) continue;
          const newDur = Math.max(0.03125, note.durationBeats * factor);
          await rpc.call("project.setNoteDuration", { noteId, durationBeats: newDur });
        }
        await rpc.call("project.endTransaction");
        useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
        useProjectStore.setState({ isDirty: true });
      } catch (err) {
        console.warn("scale duration failed", err);
      }
    },
    [activeClip, selectedNoteIds]
  );

  // ---- Clip-level operations (from former ClipEditor) ----
  const handleClipGain = useCallback(async (v: number) => {
    if (!activeClip) return;
    await rpc.call("project.setClipGain", { clipId: activeClip.clipId, gain: v }).catch(() => {});
  }, [activeClip]);

  const handleTransposeAll = useCallback(async (semitones: number) => {
    if (!activeClip || semitones === 0) return;
    const notes = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
    if (notes.length === 0) return;
    try {
      await rpc.call("project.beginTransaction", { name: `transpose ${semitones >= 0 ? "+" : ""}${semitones}` });
      for (const n of notes) {
        await rpc.call("project.setNotePitch", { noteId: n.noteId, pitch: clamp(n.pitch + semitones, 0, 127) });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip transpose failed", err);
    }
  }, [activeClip]);

  const handleQuantizeAll = useCallback(async () => {
    if (!activeClip) return;
    const { snapEnabled, snapDivision } = useUiStore.getState();
    if (!snapEnabled) return;
    const notes = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
    if (notes.length === 0) return;
    const strength = clipQuantStrength / 100;
    try {
      await rpc.call("project.beginTransaction", { name: "quantize clip" });
      for (const n of notes) {
        const newStart = quantizeWithGroove(n.startBeat, snapDivision, strength, 0);
        await rpc.call("project.setNoteStart", { noteId: n.noteId, startBeat: newStart });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip quantize failed", err);
    }
  }, [activeClip, clipQuantStrength]);

  const handleApplyVelOffset = useCallback(async () => {
    if (!activeClip || velOffset === 0) return;
    const notes = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
    if (notes.length === 0) return;
    try {
      await rpc.call("project.beginTransaction", { name: `velocity ${velOffset >= 0 ? "+" : ""}${velOffset}` });
      for (const n of notes) {
        await rpc.call("project.setNoteVelocity", { noteId: n.noteId, velocity: clamp(n.velocity + velOffset, 1, 127) });
      }
      await rpc.call("project.endTransaction");
      useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip velocity offset failed", err);
    }
  }, [activeClip, velOffset]);

  const handleHumanizeAll = useCallback(async () => {
    if (!activeClip) return;
    const notes = useProjectStore.getState().notesByClip.get(activeClip.clipId) ?? [];
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
      useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
      useProjectStore.setState({ isDirty: true });
    } catch (err) {
      console.warn("clip humanize failed", err);
    }
  }, [activeClip]);

  const keys = useMemo(() => {
    const k: { note: number; name: string; isBlack: boolean }[] = [];
    for (let n = 127; n >= 0; n--) {
      const name = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][n % 12];
      const isBlack = name.includes("#");
      k.push({ note: n, name: `${name}${Math.floor(n / 12) - 1}`, isBlack });
    }
    return k;
  }, []);

  return (
    <div className="piano-roll">
      <div className="pr-toolbar">
        {midiClips.length === 0 && <span className="pr-empty">No MIDI clips</span>}
        {midiClips.length > 0 && (
          <select
            className="pr-clip-select"
            value={activeClip?.clipId ?? ""}
            onChange={(e) => loadNotes(Number(e.target.value))}
          >
            {midiClips.map((c) => (
              <option key={c.clipId} value={c.clipId}>{c.name ?? `Clip ${c.clipId}`}</option>
            ))}
          </select>
        )}
        <span className="pr-toolbar-sep" />
        <div className="pr-zoom-group">
          <button className="pr-zoom-btn" onClick={() => zoomBy(1 / 1.25)} title="Zoom Out (Ctrl+wheel to zoom)">−</button>
          <button className="pr-zoom-btn" onClick={zoomFit} title="Fit Clip to View">⟷</button>
          <button className="pr-zoom-btn" onClick={() => zoomBy(1.25)} title="Zoom In (Ctrl+wheel to zoom)">+</button>
        </div>
        <span className="pr-toolbar-sep" />
        <label className="pr-chord-toggle">
          <input
            type="checkbox"
            checked={chordEnabled}
            onChange={(e) => setChordEnabled(e.target.checked)}
          />
          Chord
        </label>
        {chordEnabled && (
          <select value={chordType} onChange={(e) => setChordType(e.target.value)}>
            {Object.keys(CHORD_SHAPES).map((name) => (
              <option key={name} value={name}>{name}</option>
            ))}
          </select>
        )}
        {activeClip && (
          <>
            <span className="pr-toolbar-sep" />
            <label className="pr-chord-toggle">
              <input
                type="checkbox"
                checked={activeClip.looping}
                onChange={handleToggleLooping}
              />
              Loop
            </label>
          </>
        )}
        <div className="pr-pill-group">
          {activeClip && (
            <div className="pr-popover-wrap" ref={clipWrapRef}>
              <button
                className={`pr-pill ${clipOpen ? "pr-pill--active" : ""}`}
                aria-pressed={clipOpen}
                aria-haspopup="true"
                aria-expanded={clipOpen}
                title="Clip controls (gain, transpose, quantize, humanize)"
                aria-label="Clip controls"
                onClick={() => setClipOpen((o) => !o)}
              >Clip</button>
              {clipOpen && (
                <div className="pr-clip-popover">
                  <div className="pr-clip-row">
                    <label>Gain</label>
                    <input type="range" min={0} max={2} step={0.01} value={clipGain}
                      onChange={(e) => { const v = parseFloat(e.target.value); setClipGain(v); handleClipGain(v); }} />
                    <span className="pr-slider-val">{clipGain.toFixed(2)}x</span>
                  </div>
                  <div className="pr-clip-row">
                    <label>Transpose</label>
                    <button className="pr-clip-btn" onClick={() => handleTransposeAll(-12)} title="Down an octave">-12</button>
                    <button className="pr-clip-btn" onClick={() => handleTransposeAll(-1)} title="Down a semitone">-1</button>
                    <button className="pr-clip-btn" onClick={() => handleTransposeAll(1)} title="Up a semitone">+1</button>
                    <button className="pr-clip-btn" onClick={() => handleTransposeAll(12)} title="Up an octave">+12</button>
                    <input type="number" min={-48} max={48} step={1} value={transposeAmt}
                      onChange={(e) => setTransposeAmt(parseInt(e.target.value) || 0)} className="pr-clip-num" />
                    <button className="pr-clip-btn" onClick={() => handleTransposeAll(transposeAmt)}>Apply</button>
                  </div>
                  <div className="pr-clip-row">
                    <label>Quantize</label>
                    <input type="range" min={0} max={100} step={1} value={clipQuantStrength}
                      onChange={(e) => setClipQuantStrength(Number(e.target.value))} className="pr-clip-slider" />
                    <span className="pr-slider-val">{clipQuantStrength}%</span>
                    <button className="pr-clip-btn" onClick={handleQuantizeAll}
                      title={useUiStore.getState().snapEnabled ? "Quantize all notes to snap grid" : "Enable snap first"}>
                      Apply
                    </button>
                  </div>
                  <div className="pr-clip-row">
                    <label>Velocity</label>
                    <input type="range" min={-50} max={50} step={1} value={velOffset}
                      onChange={(e) => setVelOffset(Number(e.target.value))} className="pr-clip-slider" />
                    <span className="pr-slider-val">{velOffset >= 0 ? `+${velOffset}` : velOffset}</span>
                    <button className="pr-clip-btn" onClick={handleApplyVelOffset} disabled={velOffset === 0}>Apply</button>
                  </div>
                  <div className="pr-clip-row">
                    <label>Humanize</label>
                    <button className="pr-clip-btn" onClick={handleHumanizeAll}
                      title="Add slight timing + velocity randomness to all notes">
                      Humanize All
                    </button>
                  </div>
                </div>
              )}
            </div>
          )}
          <button
            className={`pr-pill ${velOpen ? "pr-pill--active" : ""}`}
            aria-pressed={velOpen}
            title="Toggle velocity lane"
            aria-label="Toggle velocity lane"
            onClick={() => setVelOpen((v) => !v)}
          >Vel</button>
          <div className="pr-popover-wrap" ref={ccWrapRef}>
            <button
              className={`pr-pill ${ccOpen ? "pr-pill--active" : ""}`}
              aria-pressed={ccOpen}
              aria-haspopup="true"
              aria-expanded={ccOpen}
              title="Add CC lane"
              aria-label="Add CC lane"
              onClick={() => setCcOpen((o) => !o)}
            >+CC</button>
            {ccOpen && (
              <div className="pr-cc-popover">
                <select value={ccToAdd} onChange={(e) => setCcToAdd(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, i) => (
                    <option key={i} value={i}>CC{i}</option>
                  ))}
                </select>
                <button
                  className="pr-cc-add-btn"
                  onClick={() => {
                    setCcLanes((prev) => (prev.includes(ccToAdd) ? prev : [...prev, ccToAdd]));
                    setCcOpen(false);
                  }}
                >Add</button>
              </div>
            )}
          </div>
          {selectedNoteIds.size > 0 && (
            <div className="pr-popover-wrap" ref={editWrapRef}>
              <button
                className={`pr-pill ${editOpen ? "pr-pill--active" : ""}`}
                aria-pressed={editOpen}
                aria-haspopup="true"
                aria-expanded={editOpen}
                title="Edit selected notes"
                aria-label="Edit selected notes"
                onClick={() => setEditOpen((o) => !o)}
              >Edit</button>
              {editOpen && (
                <div className="pr-edit-popover">
                  <label className="pr-slider-ctrl">
                    <span className="pr-slider-label">Vel</span>
                    <input
                      type="range"
                      min={10}
                      max={200}
                      defaultValue={100}
                      className="pr-slider"
                      onMouseUp={(e) => {
                        const factor = Number((e.target as HTMLInputElement).value) / 100;
                        handleScaleVelocity(factor);
                        (e.target as HTMLInputElement).value = "100";
                      }}
                    />
                  </label>
                  <label className="pr-slider-ctrl">
                    <span className="pr-slider-label">Dur</span>
                    <input
                      type="range"
                      min={10}
                      max={200}
                      defaultValue={100}
                      className="pr-slider"
                      onMouseUp={(e) => {
                        const factor = Number((e.target as HTMLInputElement).value) / 100;
                        handleScaleDuration(factor);
                        (e.target as HTMLInputElement).value = "100";
                      }}
                    />
                  </label>
                  <label className="pr-slider-ctrl">
                    <span className="pr-slider-label">Q.Str</span>
                    <input
                      type="range"
                      min={0}
                      max={100}
                      value={quantizeStrength}
                      className="pr-slider"
                      onChange={(e) => setQuantizeStrength(Number(e.target.value))}
                    />
                    <span className="pr-slider-val">{quantizeStrength}%</span>
                  </label>
                  <label className="pr-slider-ctrl">
                    <span className="pr-slider-label">Swing</span>
                    <input
                      type="range"
                      min={0}
                      max={100}
                      value={Math.round(swing * 100)}
                      className="pr-slider"
                      onChange={(e) => setSwing(Number(e.target.value) / 100)}
                    />
                    <span className="pr-slider-val">{Math.round(swing * 100)}%</span>
                  </label>
                </div>
              )}
            </div>
          )}
        </div>
      </div>
      <div className="pr-editor">
        <div className="pr-keys" ref={keysRef}>
          {keys.map((k) => (
            <div
              key={k.note}
              className={`pr-key ${k.isBlack ? "pr-key--black" : "pr-key--white"} ${k.note % 12 === 0 ? "pr-key--c" : ""}`}
            >
              {k.note % 12 === 0 && <span className="pr-key-label">{k.name}</span>}
            </div>
          ))}
        </div>
        <div className="pr-grid-area">
          <NoteGrid
            notes={notes}
            rpc={rpc}
            clipId={activeClip?.clipId ?? null}
            pixelsPerBeat={pixelsPerBeat}
            onVerticalScroll={handleGridScroll}
            onHorizontalScroll={setGridScrollLeft}
            onZoom={handleZoom}
            selectedNoteIds={selectedNoteIds}
            onSelectionChange={setSelectedNoteIds}
            chordShape={chordEnabled ? CHORD_SHAPES[chordType] : undefined}
            quantizeStrength={quantizeStrength}
            swing={swing}
          />
          <button
            className="pr-lane-handle"
            title={velOpen ? "Hide velocity lane" : "Show velocity lane"}
            aria-label={velOpen ? "Hide velocity lane" : "Show velocity lane"}
            onClick={() => setVelOpen((v) => !v)}
          />
          {velOpen && (
            <VelocityLane
              notes={notes}
              selectedNoteIds={selectedNoteIds}
              rpc={rpc}
              pixelsPerBeat={pixelsPerBeat}
              onVelocityChange={handleVelocityChange}
              scrollLeft={gridScrollLeft}
              onScrollChange={setGridScrollLeft}
            />
          )}
          {ccLanes.length > 0 && activeClip && (
            <div className="pr-cc-row">
              {ccLanes.map((cc) => (
                <CCLane
                  key={cc}
                  clipId={activeClip.clipId}
                  controllerNumber={cc}
                  width={gridWidth}
                  pixelsPerBeat={pixelsPerBeat}
                  scrollX={gridScrollLeft}
                  onRemove={() => setCcLanes((prev) => prev.filter((c) => c !== cc))}
                />
              ))}
            </div>
          )}
        </div>
        {selectedNoteIds.size > 0 && activeClip && (
          <NoteOperatorsPane
            selectedNoteIds={selectedNoteIds}
            notes={notes}
            activeClip={activeClip}
            rpc={rpc}
            onRefresh={() => {
              if (activeClip) {
                useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
              }
            }}
          />
        )}
      </div>
    </div>
  );
}
