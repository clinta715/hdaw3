import React from "react";
import { useAnalysisStore } from "../store/analysisStore";
import { useProjectStore } from "../store/projectStore";
import "./FmAnalysisPanel.css";

const OP_LABELS = ["1", "2", "3", "4", "5", "6"];

export default function FmAnalysisPanel() {
  const tracks = useAnalysisStore((s) => s.tracks);
  const snapshot = useProjectStore((s) => s.snapshot);

  // Find the first track that has FM analysis data (non-zero levels)
  const trackIndex =
    snapshot?.tracks?.findIndex(
      (_, i) => tracks[i] && tracks[i].activeVoices > 0
    ) ?? -1;

  const data = trackIndex >= 0 ? tracks[trackIndex] : null;

  if (!data || data.activeVoices === 0) {
    return (
      <div className="fm-analysis">
        <div className="fm-analysis__empty">
          No active FM synth — add an FM Synth FX to a track to see operator
          levels.
        </div>
      </div>
    );
  }

  return (
    <div className="fm-analysis">
      <div className="fm-analysis__header">
        <span className="fm-analysis__label">FM Analysis</span>
        <span className="fm-analysis__voices">
          {data.activeVoices} voice
          {data.activeVoices !== 1 ? "s" : ""}
        </span>
        <span className="fm-analysis__algo">Alg {data.algorithm + 1}</span>
      </div>
      <div className="fm-analysis__bars">
        {OP_LABELS.map((label, i) => {
          const level = data.opEgLevel[i] ?? 0;
          const pct = Math.round(level * 100);
          let cls = "fm-bar__fill";
          if (pct > 90) cls += " fm-bar__fill--clip";
          else if (pct > 75) cls += " fm-bar__fill--hot";
          return (
            <div key={i} className="fm-bar">
              <div className="fm-bar__label">Op {label}</div>
              <div className="fm-bar__meter">
                <div className={cls} style={{ height: `${pct}%` }} />
              </div>
              <div className="fm-bar__value">{pct}%</div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
