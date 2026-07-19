import { useState, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./StepSequencer.css";

const ROWS = 8;
const STEPS = 16;
const BASE_NOTE = 48; // C3

export default function StepSequencer() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const [steps, setSteps] = useState<boolean[][]>(
    Array.from({ length: ROWS }, () => Array(STEPS).fill(false))
  );

  const midiClip = snapshot?.clips.find(
    (c) => selectedClipIds.has(c.clipId) && c.isMidi
  );

  const toggleStep = useCallback((row: number, col: number) => {
    setSteps((prev) => {
      const next = prev.map((r) => [...r]);
      next[row][col] = !next[row][col];
      return next;
    });
  }, []);

  const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  return (
    <div className="step-sequencer">
      <div className="ss-header">
        <span className="ss-title">Step Sequencer</span>
        {midiClip && (
          <span className="ss-clip-name">{midiClip.name}</span>
        )}
      </div>
      <div className="ss-grid">
        {Array.from({ length: ROWS }, (_, row) => (
          <div key={row} className="ss-row">
            <div className="ss-note-label">
              {noteNames[(BASE_NOTE + (ROWS - 1 - row)) % 12]}
              {Math.floor((BASE_NOTE + (ROWS - 1 - row)) / 12) - 1}
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
