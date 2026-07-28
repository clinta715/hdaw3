import { useState, useRef, useEffect } from "react";
import { rpc } from "../rpc";
import { TRACK_TYPE_COLORS, TRACK_TYPE_ICONS } from "../utils/trackTypes";
import "./AddTrackMenu.css";

interface Props {
  /** Trigger content (e.g. "+" or "+ Add Track"). Defaults to "+". */
  label?: React.ReactNode;
  /** Extra class for the trigger button. */
  triggerClassName?: string;
  title?: string;
}

// A small "add track" affordance that prompts for the track type. Clicking the
// trigger opens a popover with Audio / MIDI options so a new track is never
// silently typed — the two kinds are logically distinct and the user picks.
export function AddTrackMenu({ label = "+", triggerClassName, title = "Add Track" }: Props) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, [open]);

  const add = (trackType: number) => {
    rpc.call("project.addTrack", { trackType }).catch(() => {});
    setOpen(false);
  };

  return (
    <div className="add-track-menu" ref={ref}>
      <button
        type="button"
        className={`add-track-trigger${triggerClassName ? ` ${triggerClassName}` : ""}`}
        title={title}
        onClick={() => setOpen((o) => !o)}
      >
        {label}
      </button>
      {open && (
        <div className="add-track-popover">
          <button type="button" className="add-track-opt" onClick={() => add(0)}>
            <span
              className="add-track-chip"
              style={{ background: TRACK_TYPE_COLORS[0] }}
            >
              {TRACK_TYPE_ICONS[0]}
            </span>
            Audio Track
          </button>
          <button type="button" className="add-track-opt" onClick={() => add(1)}>
            <span
              className="add-track-chip"
              style={{ background: TRACK_TYPE_COLORS[1] }}
            >
              {TRACK_TYPE_ICONS[1]}
            </span>
            MIDI Track
          </button>
        </div>
      )}
    </div>
  );
}
