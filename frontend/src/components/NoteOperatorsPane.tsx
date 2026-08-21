import { useState, useMemo, useCallback, useRef, useEffect } from "react";
import { NoteSnapshot } from "../rpc/types";
import { RpcClient } from "../rpc/client";
import "./NoteOperatorsPane.css";

interface Props {
  selectedNoteIds: Set<number>;
  notes: NoteSnapshot[];
  activeClip: { clipId: number; seed?: number } | null;
  rpc: RpcClient;
  onRefresh: () => void;
}

interface MixedState {
  chance: number | null;
  repeatCount: number | null;
  repeatRate: number | null;
  repeatCurve: number | null;
  occurrence: number | null;
  recurrence: number | null;
  noteGain: number | null;
  notePan: number | null;
  notePitch: number | null;
  noteTimbre: number | null;
  notePressure: number | null;
}

function computeMixed(notes: NoteSnapshot[]): MixedState {
  if (notes.length === 0) {
    return {
      chance: null, repeatCount: null, repeatRate: null, repeatCurve: null,
      occurrence: null, recurrence: null, noteGain: null, notePan: null,
      notePitch: null, noteTimbre: null, notePressure: null,
    };
  }
  const first = notes[0];
  const same = (key: keyof NoteSnapshot) => {
    const v = first[key];
    return notes.every((n) => n[key] === v);
  };
  return {
    chance: same("chance") ? first.chance : null,
    repeatCount: same("repeatCount") ? first.repeatCount : null,
    repeatRate: same("repeatRate") ? first.repeatRate : null,
    repeatCurve: same("repeatCurve") ? first.repeatCurve : null,
    occurrence: same("occurrence") ? first.occurrence : null,
    recurrence: same("recurrence") ? first.recurrence : null,
    noteGain: same("noteGain") ? first.noteGain : null,
    notePan: same("notePan") ? first.notePan : null,
    notePitch: same("notePitch") ? first.notePitch : null,
    noteTimbre: same("noteTimbre") ? first.noteTimbre : null,
    notePressure: same("notePressure") ? first.notePressure : null,
  };
}

function useDebouncedCallback(fn: () => void, delay: number) {
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const fnRef = useRef(fn);
  fnRef.current = fn;

  useEffect(() => {
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, []);

  return useCallback(() => {
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      timerRef.current = null;
      fnRef.current();
    }, delay);
  }, [delay]);
}

export default function NoteOperatorsPane({ selectedNoteIds, notes, activeClip, rpc, onRefresh }: Props) {
  const [collapsed, setCollapsed] = useState(() => {
    try {
      return localStorage.getItem("noteOperatorsCollapsed") === "true";
    } catch {
      return false;
    }
  });

  const toggleCollapsed = useCallback(() => {
    setCollapsed((prev) => {
      const next = !prev;
      try {
        localStorage.setItem("noteOperatorsCollapsed", String(next));
      } catch { /* ignore */ }
      return next;
    });
  }, []);

  const selectedNotes = useMemo(
    () => notes.filter((n) => selectedNoteIds.has(n.noteId)),
    [notes, selectedNoteIds],
  );

  const mixed = useMemo(() => computeMixed(selectedNotes), [selectedNotes]);
  const multi = selectedNotes.length > 1;

  const applySingle = useCallback(
    async (method: string, params: Record<string, unknown>) => {
      try {
        await rpc.call(method, params);
        onRefresh();
      } catch (err) {
        console.warn(`${method} failed`, err);
      }
    },
    [rpc, onRefresh],
  );

  const applyBulk = useCallback(
    async (method: string, paramKey: string, value: unknown) => {
      if (selectedNotes.length === 0) return;
      try {
        await rpc.call("project.beginTransaction", { name: `set ${paramKey}` });
        for (const note of selectedNotes) {
          await rpc.call(method, { noteId: note.noteId, [paramKey]: value });
        }
        await rpc.call("project.endTransaction");
        onRefresh();
      } catch (err) {
        console.warn(`bulk ${method} failed`, err);
      }
    },
    [rpc, selectedNotes, onRefresh],
  );

  const apply = useCallback(
    (method: string, paramKey: string, value: unknown) => {
      if (multi) {
        applyBulk(method, paramKey, value);
      } else if (selectedNotes.length === 1) {
        applySingle(method, { noteId: selectedNotes[0].noteId, [paramKey]: value });
      }
    },
    [multi, selectedNotes, applySingle, applyBulk],
  );

  const debouncedRefresh = useDebouncedCallback(onRefresh, 100);

  const applySlider = useCallback(
    (method: string, paramKey: string, value: number) => {
      if (multi) {
        (async () => {
          try {
            await rpc.call("project.beginTransaction", { name: `set ${paramKey}` });
            for (const note of selectedNotes) {
              await rpc.call(method, { noteId: note.noteId, [paramKey]: value });
            }
            await rpc.call("project.endTransaction");
            debouncedRefresh();
          } catch (err) {
            console.warn(`bulk ${method} failed`, err);
          }
        })();
      } else if (selectedNotes.length === 1) {
        rpc.call(method, { noteId: selectedNotes[0].noteId, [paramKey]: value })
          .then(() => debouncedRefresh())
          .catch((err) => console.warn(`${method} failed`, err));
      }
    },
    [multi, selectedNotes, rpc, debouncedRefresh],
  );

  const handleSeedChange = useCallback(
    (seed: number) => {
      if (!activeClip) return;
      rpc.call("project.setClipSeed", { clipId: activeClip.clipId, seed })
        .then(() => onRefresh())
        .catch((err) => console.warn("setClipSeed failed", err));
    },
    [rpc, activeClip, onRefresh],
  );

  const fmt = (v: number | null, decimals: number, suffix = "", multiplier = 1) =>
    v === null ? "\u2014" : `${(v * multiplier).toFixed(decimals)}${suffix}`;

  const fmtInt = (v: number | null) =>
    v === null ? "\u2014" : String(v);

  return (
    <div className="nop-pane">
      <div className="nop-header" onClick={toggleCollapsed} role="button" tabIndex={0}
        onKeyDown={(e) => { if (e.key === "Enter" || e.key === " ") toggleCollapsed(); }}
      >
        <span className="nop-header-title">Note Operators</span>
        <span className={`nop-header-arrow ${collapsed ? "nop-header-arrow--collapsed" : ""}`} aria-hidden="true">&#9660;</span>
      </div>
      <div className={`nop-body ${collapsed ? "nop-body--collapsed" : ""}`}>
        {activeClip && (
          <div className="nop-seed-row">
            <span className="nop-seed-label">Clip Seed</span>
            <input
              type="number"
              className="nop-seed-input"
              value={activeClip.seed ?? 0}
              onChange={(e) => handleSeedChange(Number(e.target.value))}
            />
          </div>
        )}

        <div className="nop-section">
          <div className="nop-section-title">Operators</div>

          <div className="nop-row">
            <span className="nop-label">Chance</span>
            <input
              type="range"
              className="nop-slider"
              min={0}
              max={1}
              step={0.01}
              value={mixed.chance ?? 1}
              disabled={mixed.chance === null}
              onChange={(e) => {
                applySlider("project.setNoteChance", "chance", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.chance === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.chance, 0, "%", 100)}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Repeat Cnt</span>
            <input
              type="number"
              className="nop-input"
              min={0}
              value={mixed.repeatCount ?? ""}
              placeholder={multi ? "\u2014" : "0"}
              onChange={(e) => apply("project.setNoteRepeatCount", "repeatCount", Number(e.target.value))}
            />
          </div>

          <div className="nop-row">
            <span className="nop-label">Repeat Rate</span>
            <input
              type="number"
              className="nop-input"
              min={0}
              step={0.01}
              value={mixed.repeatRate ?? ""}
              placeholder={multi ? "\u2014" : "0.25"}
              onChange={(e) => apply("project.setNoteRepeatRate", "repeatRate", Number(e.target.value))}
            />
            <span className="nop-value">beats</span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Repeat Curve</span>
            <input
              type="range"
              className="nop-slider"
              min={-1}
              max={1}
              step={0.01}
              value={mixed.repeatCurve ?? 0}
              disabled={mixed.repeatCurve === null}
              onChange={(e) => {
                applySlider("project.setNoteRepeatCurve", "repeatCurve", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.repeatCurve === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.repeatCurve, 2)}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Occurrence</span>
            <input
              type="number"
              className="nop-input"
              min={0}
              value={mixed.occurrence ?? ""}
              placeholder={multi ? "\u2014" : "0"}
              onChange={(e) => apply("project.setNoteOccurrence", "occurrence", Number(e.target.value))}
            />
          </div>

          <div className="nop-row">
            <span className="nop-label">Recurrence</span>
            <input
              type="number"
              className="nop-input"
              min={0}
              max={2}
              value={mixed.recurrence ?? ""}
              placeholder={multi ? "\u2014" : "0"}
              onChange={(e) => apply("project.setNoteRecurrence", "recurrence", Number(e.target.value))}
            />
          </div>
        </div>

        <div className="nop-section">
          <div className="nop-section-title">Expression</div>

          <div className="nop-row">
            <span className="nop-label">Gain</span>
            <input
              type="range"
              className="nop-slider"
              min={0}
              max={2}
              step={0.01}
              value={mixed.noteGain ?? 1}
              disabled={mixed.noteGain === null}
              onChange={(e) => {
                applySlider("project.setNoteGain", "gain", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.noteGain === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.noteGain, 2)}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Pan</span>
            <input
              type="range"
              className="nop-slider"
              min={-1}
              max={1}
              step={0.01}
              value={mixed.notePan ?? 0}
              disabled={mixed.notePan === null}
              onChange={(e) => {
                applySlider("project.setNotePan", "pan", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.notePan === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.notePan, 2)}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Pitch</span>
            <input
              type="range"
              className="nop-slider"
              min={-24}
              max={24}
              step={0.1}
              value={mixed.notePitch ?? 0}
              disabled={mixed.notePitch === null}
              onChange={(e) => {
                applySlider("project.setNotePitchOffset", "pitchOffset", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.notePitch === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.notePitch, 1, " st")}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Timbre</span>
            <input
              type="range"
              className="nop-slider"
              min={0}
              max={1}
              step={0.01}
              value={mixed.noteTimbre ?? 0.5}
              disabled={mixed.noteTimbre === null}
              onChange={(e) => {
                applySlider("project.setNoteTimbre", "timbre", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.noteTimbre === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.noteTimbre, 2)}
            </span>
          </div>

          <div className="nop-row">
            <span className="nop-label">Pressure</span>
            <input
              type="range"
              className="nop-slider"
              min={0}
              max={1}
              step={0.01}
              value={mixed.notePressure ?? 0}
              disabled={mixed.notePressure === null}
              onChange={(e) => {
                applySlider("project.setNotePressure", "pressure", Number(e.target.value));
              }}
            />
            <span className={`nop-value ${mixed.notePressure === null ? "nop-value--mixed" : ""}`}>
              {fmt(mixed.notePressure, 2)}
            </span>
          </div>
        </div>
      </div>
    </div>
  );
}
