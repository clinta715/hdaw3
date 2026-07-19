import { useState, useMemo, useRef, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";
import NoteGrid from "./NoteGrid";
import "./PianoRoll.css";

export default function PianoRoll() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const notesByClip = useProjectStore((s) => s.notesByClip);
  const [selectedClipId, setSelectedClipId] = useState<number | null>(null);
  const keysRef = useRef<HTMLDivElement>(null);

  const midiClips = snapshot?.clips.filter((c) => c.isMidi) ?? [];
  const activeClip = selectedClipId != null
    ? midiClips.find((c) => c.clipId === selectedClipId)
    : midiClips[0];

  const notes = activeClip ? notesByClip.get(activeClip.clipId) ?? [] : [];

  const loadNotes = (clipId: number) => {
    setSelectedClipId(clipId);
    if (!notesByClip.has(clipId)) {
      useProjectStore.getState().syncNotes(rpc, clipId);
    }
  };

  const handleGridScroll = useCallback((scrollTop: number) => {
    if (keysRef.current) {
      keysRef.current.scrollTop = scrollTop;
    }
  }, []);

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
        <NoteGrid notes={notes} rpc={rpc} clipId={activeClip?.clipId ?? null} onVerticalScroll={handleGridScroll} />
      </div>
    </div>
  );
}
