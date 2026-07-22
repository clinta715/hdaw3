import { useState, useCallback, useEffect, useRef } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { reportRpcError } from "../store/notifyStore";
import { rpc } from "../rpc";
import { NoteSnapshot } from "../rpc/types";
import "./StepSequencer.css";

const ROWS = 8;
const STEPS = 16;
const BASE_NOTE = 48; // C3
const STEP_BEATS = 1 / 16;  // each column is a 1/16 note
const DEFAULT_VELOCITY = 96;

// Map a grid cell to the pitch it represents. Row 0 = top = highest pitch.
function cellPitch(row: number): number {
  return BASE_NOTE + (ROWS - 1 - row);
}
// Map a grid cell to its start beat (column 0 = beat 0).
function cellStartBeat(col: number): number {
  return col * STEP_BEATS;
}

export default function StepSequencer() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const [steps, setSteps] = useState<boolean[][]>(
    Array.from({ length: ROWS }, () => Array(STEPS).fill(false))
  );
  const lastLoadedClipId = useRef<number | null>(null);

  const midiClip = snapshot?.clips.find(
    (c) => selectedClipIds.has(c.clipId) && c.isMidi
  );

  // Populate the grid from existing MIDI notes whenever the selected clip
  // changes. Notes whose pitch is in our row range and whose startBeat lines
  // up with a column are turned on; everything else is left off.
  useEffect(() => {
    const clipId = midiClip?.clipId ?? null;
    if (clipId == null) {
      if (lastLoadedClipId.current !== null) {
        setSteps(Array.from({ length: ROWS }, () => Array(STEPS).fill(false)));
        lastLoadedClipId.current = null;
      }
      return;
    }
    if (lastLoadedClipId.current === clipId) return;
    lastLoadedClipId.current = clipId;

    rpc.call("read.getNotes", { clipId })
      .then((data) => {
        if (!Array.isArray(data)) return;
        const next = Array.from({ length: ROWS }, () => Array(STEPS).fill(false));
        for (const n of data as NoteSnapshot[]) {
          const row = ROWS - 1 - (n.pitch - BASE_NOTE);
          if (row < 0 || row >= ROWS) continue;
          const col = Math.round(n.startBeat / STEP_BEATS);
          if (col < 0 || col >= STEPS) continue;
          if (Math.abs(n.startBeat - col * STEP_BEATS) < STEP_BEATS / 4)
            next[row][col] = true;
        }
        setSteps(next);
      })
      .catch((err) => reportRpcError("read.getNotes", err));
  }, [midiClip?.clipId]);

  const toggleStep = useCallback((row: number, col: number) => {
    const clipId = midiClip?.clipId;
    if (clipId == null) return;
    const wasOn = steps[row][col];
    const pitch = cellPitch(row);
    const startBeat = cellStartBeat(col);

    // Optimistic local flip
    setSteps((prev) => {
      const next = prev.map((r) => [...r]);
      next[row][col] = !next[row][col];
      return next;
    });

    // Rollback the optimistic flip on RPC failure
    const rollback = () => {
      setSteps((prev) => {
        const next = prev.map((r) => [...r]);
        next[row][col] = wasOn;
        return next;
      });
    };

    if (wasOn) {
      // Turn off: find the note at this pitch+beat and remove it.
      rpc.call("read.getNotes", { clipId })
        .then((data) => {
          if (!Array.isArray(data)) { rollback(); return; }
          const match = (data as NoteSnapshot[]).find(
            (n) => n.pitch === pitch && Math.abs(n.startBeat - startBeat) < STEP_BEATS / 4
          );
          if (match != null) {
            rpc.call("project.removeNote", { noteId: match.noteId })
              .catch((err) => { reportRpcError("project.removeNote", err); rollback(); });
          }
        })
        .catch((err) => { reportRpcError("read.getNotes", err); rollback(); });
    } else {
      // Turn on: add a 1/16-length note.
      rpc.call("project.addNote", {
        clipId,
        pitch,
        velocity: DEFAULT_VELOCITY,
        startBeat,
        durationBeats: STEP_BEATS,
      }).catch((err) => { reportRpcError("project.addNote", err); rollback(); });
    }
  }, [midiClip?.clipId, steps]);

  const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  return (
    <div className="step-sequencer">
      <div className="ss-header">
        <span className="ss-title">Step Sequencer</span>
        {midiClip ? (
          <span className="ss-clip-name">{midiClip.name}</span>
        ) : (
          <span className="ss-empty-hint">Select a MIDI clip to edit</span>
        )}
      </div>
      <div className="ss-grid">
        {Array.from({ length: ROWS }, (_, row) => (
          <div key={row} className="ss-row">
            <div className="ss-note-label">
              {noteNames[cellPitch(row) % 12]}
              {Math.floor(cellPitch(row) / 12) - 1}
            </div>
            {Array.from({ length: STEPS }, (_, col) => (
              <div
                key={col}
                className={`ss-cell${steps[row][col] ? " active" : ""}${col % 4 === 0 ? " beat" : ""}`}
                onClick={() => toggleStep(row, col)}
              />
            ))}
          </div>
        ))}
        <div className="ss-step-labels">
          <div className="ss-note-label" />
          {Array.from({ length: STEPS }, (_, i) => (
            <div key={i} className="ss-step-num">{i + 1}</div>
          ))}
        </div>
      </div>
    </div>
  );
}
