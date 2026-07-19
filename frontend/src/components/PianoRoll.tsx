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
    return Math.max(800, Math.ceil(maxEnd * 80) + 200);
  }, [notes]);

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
            onVerticalScroll={handleGridScroll}
            onHorizontalScroll={setGridScrollLeft}
            selectedNoteIds={selectedNoteIds}
            onSelectionChange={setSelectedNoteIds}
            chordShape={chordEnabled ? CHORD_SHAPES[chordType] : undefined}
          />
          <VelocityLane
            notes={notes}
            selectedNoteIds={selectedNoteIds}
            rpc={rpc}
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
                pixelsPerBeat={80}
                scrollX={gridScrollLeft}
              />
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
