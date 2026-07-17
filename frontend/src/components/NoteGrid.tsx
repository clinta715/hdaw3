import { useMemo } from "react";
import { NoteSnapshot } from "../rpc/types";
import "./NoteGrid.css";

interface Props {
  notes: NoteSnapshot[];
}

const KEY_HEIGHT = 8;
const PIXELS_PER_BEAT = 80;

export default function NoteGrid({ notes }: Props) {
  const rects = useMemo(() => {
    if (!notes.length) return [];
    let maxEnd = 0;
    for (const n of notes) {
      const end = n.startBeat + n.durationBeats;
      if (end > maxEnd) maxEnd = end;
    }
    const gridW = Math.max(maxEnd * PIXELS_PER_BEAT, 400);

    return notes.map((n) => {
      const x = n.startBeat * PIXELS_PER_BEAT;
      const y = (127 - n.pitch) * KEY_HEIGHT;
      const w = Math.max(2, n.durationBeats * PIXELS_PER_BEAT);
      const h = KEY_HEIGHT - 1;
      return { x, y, w, h, note: n.pitch, vel: n.velocity };
    });
  }, [notes]);

  return (
    <div className="note-grid">
      {rects.length === 0 && (
        <div className="ng-empty">No notes</div>
      )}
      {rects.map((r, i) => (
        <div
          key={i}
          className="ng-note"
          style={{
            left: r.x,
            top: r.y,
            width: r.w,
            height: r.h,
            opacity: 0.4 + r.vel * 0.6,
          }}
        />
      ))}
    </div>
  );
}
