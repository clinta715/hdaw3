import { useState, useMemo, useRef, useCallback, useEffect } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import NoteGrid from "./NoteGrid";
import VelocityLane from "./VelocityLane";
import CCLane from "./CCLane";
import "./PianoRoll.css";

export default function PianoRoll() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const notesByClip = useProjectStore((s) => s.notesByClip);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const [internalClipId, setInternalClipId] = useState<number | null>(null);
  const keysRef = useRef<HTMLDivElement>(null);
  const [selectedNoteIds, setSelectedNoteIds] = useState<Set<number>>(new Set());
  const [gridScrollLeft, setGridScrollLeft] = useState(0);
  const [ccController, setCcController] = useState(1);
  const [chordEnabled, setChordEnabled] = useState(false);
  const [chordType, setChordType] = useState("major");
  const [pixelsPerBeat, setPixelsPerBeat] = useState(80);
  const [quantizeStrength, setQuantizeStrength] = useState(100);

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

  // Auto-load notes when a new clip becomes active
  useEffect(() => {
    if (activeClip && !notesByClip.has(activeClip.clipId)) {
      useProjectStore.getState().syncNotes(rpc, activeClip.clipId);
    }
  }, [activeClip?.clipId, notesByClip]);

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
        {midiClips.map((c) => (
          <button
            key={c.clipId}
            className={`pr-clip-btn ${c.clipId === activeClip?.clipId ? "active" : ""}`}
            onClick={() => loadNotes(c.clipId)}
          >
            {c.name ?? `Clip ${c.clipId}`}
          </button>
        ))}
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
        {selectedNoteIds.size > 0 && (
          <>
            <span className="pr-toolbar-sep" />
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
          </>
        )}
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
          />
          <VelocityLane
            notes={notes}
            selectedNoteIds={selectedNoteIds}
            rpc={rpc}
            pixelsPerBeat={pixelsPerBeat}
            onVelocityChange={handleVelocityChange}
            scrollLeft={gridScrollLeft}
            onScrollChange={setGridScrollLeft}
          />
          <div className="pr-cc-row">
            <select value={ccController} onChange={(e) => setCcController(Number(e.target.value))}>
              {Array.from({ length: 128 }, (_, i) => (
                <option key={i} value={i}>CC{i}</option>
              ))}
            </select>
            {activeClip && (
              <CCLane
                clipId={activeClip.clipId}
                controllerNumber={ccController}
                width={gridWidth}
                pixelsPerBeat={pixelsPerBeat}
                scrollX={gridScrollLeft}
              />
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
