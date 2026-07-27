import React from "react";
import { ClipSnapshot } from "../rpc/types";
import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";

interface Props {
  clip: ClipSnapshot;
  width: number;
  height: number;
}

// Draws a mini piano-roll preview of a MIDI clip's notes, fitted to the
// clip's own pitch range so a bassline or lead fills the thumbnail instead
// of collapsing to a single line. Mirrors the WaveformCanvas pattern:
// data lives in the project store's notesByClip (fetched once via syncNotes),
// and the canvas redraws when the notes reference for this clip changes.
export const MidiThumbnailCanvas: React.FC<Props> = ({ clip, width, height }) => {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  // Subscribe to just this clip's notes array — stable across other clips'
  // note edits (syncNotes builds a new Map but reuses other keys' refs).
  const notes = useProjectStore((s) => s.notesByClip.get(clip.clipId));
  const fetchingRef = React.useRef(false);

  // Lazy-load notes once per clip if they aren't present yet. The store keeps
  // the result (even an empty array), so this fires at most once per clip.
  React.useEffect(() => {
    if (notes === undefined && !fetchingRef.current) {
      fetchingRef.current = true;
      useProjectStore.getState().syncNotes(rpc, clip.clipId).finally(() => {
        fetchingRef.current = false;
      });
    }
  }, [notes, clip.clipId]);

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width <= 0 || height <= 0) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);

    if (!notes || notes.length === 0) return;

    const dur = clip.durationBeats > 0 ? clip.durationBeats : 1;

    // Fit the vertical axis to the clip's pitch range with padding.
    let minPitch = 127;
    let maxPitch = 0;
    for (const n of notes) {
      if (n.pitch < minPitch) minPitch = n.pitch;
      if (n.pitch > maxPitch) maxPitch = n.pitch;
    }
    let pad = Math.max(2, Math.ceil((maxPitch - minPitch) * 0.1));
    if (maxPitch - minPitch === 0) pad = 6;
    let lo = minPitch - pad;
    let hi = maxPitch + pad;
    if (lo < 0) { hi -= lo; lo = 0; }
    if (hi > 127) { lo -= (hi - 127); hi = 127; }
    if (lo < 0) lo = 0;
    const span = hi - lo || 1;

    const xFor = (beat: number) => (beat / dur) * width;
    const yFor = (pitch: number) => height * (1 - (pitch - lo) / span);

    for (const n of notes) {
      const x = xFor(n.startBeat);
      const noteW = Math.max(1, xFor(n.startBeat + n.durationBeats) - x);
      const y = yFor(n.pitch);
      // Row height: leave ~1px gaps so adjacent pitches read as distinct.
      const rowH = Math.max(1.5, height / span);
      const top = Math.max(0, y - rowH);
      // Velocity (1..127) maps to opacity for a sense of dynamics.
      const alpha = 0.35 + (n.velocity / 127) * 0.5;
      ctx.fillStyle = `rgba(56, 178, 223, ${alpha.toFixed(3)})`;
      ctx.fillRect(x, top, Math.min(noteW, width - x), rowH - 1);
    }
  }, [notes, clip.durationBeats, width, height]);

  return <canvas ref={canvasRef} style={{ width, height, display: "block" }} />;
};
