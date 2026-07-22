import { useRef, useCallback, useState, useEffect } from "react";
import { NoteSnapshot } from "../rpc/types";
import { RpcClient } from "../rpc/client";
import "./VelocityLane.css";

interface Props {
  notes: NoteSnapshot[];
  selectedNoteIds: Set<number>;
  rpc: RpcClient;
  onVelocityChange: (noteId: number, velocity: number) => void;
  scrollLeft?: number;
  onScrollChange?: (scrollLeft: number) => void;
}

const PIXELS_PER_BEAT = 80;
const LANE_HEIGHT = 40;

export default function VelocityLane({
  notes,
  selectedNoteIds,
  rpc,
  onVelocityChange,
  scrollLeft,
  onScrollChange,
}: Props) {
  const laneRef = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState<{ noteId: number; startY: number; startVel: number } | null>(null);

  const barWidth = 6;
  const gap = 2;

  const getBarX = (note: NoteSnapshot, index: number) => {
    return note.startBeat * PIXELS_PER_BEAT + index * (barWidth + gap);
  };

  const getBarHeight = (velocity: number) => {
    return (velocity / 127) * (LANE_HEIGHT - 4);
  };

  const handleMouseDown = useCallback(
    (e: React.MouseEvent, note: NoteSnapshot) => {
      e.preventDefault();
      e.stopPropagation();
      setDragging({ noteId: note.noteId, startY: e.clientY, startVel: note.velocity });
    },
    []
  );

  const handleMouseMove = useCallback(
    (e: globalThis.MouseEvent) => {
      if (!dragging) return;
      const deltaY = dragging.startY - e.clientY;
      const deltaVel = Math.round((deltaY / (LANE_HEIGHT - 4)) * 127);
      const newVel = Math.max(1, Math.min(127, dragging.startVel + deltaVel));
      onVelocityChange(dragging.noteId, newVel);
    },
    [dragging, onVelocityChange]
  );

  const handleMouseUp = useCallback(() => {
    setDragging(null);
  }, []);

  // Window-level move/up listeners active while dragging. Both must be window
  // level — element-level mousemove freezes the velocity drag when the cursor
  // exits the lane bounds mid-gesture, and element-level mouseup misses a
  // release outside the lane.
  useEffect(() => {
    if (dragging) {
      window.addEventListener("mousemove", handleMouseMove);
      window.addEventListener("mouseup", handleMouseUp);
      return () => {
        window.removeEventListener("mousemove", handleMouseMove);
        window.removeEventListener("mouseup", handleMouseUp);
      };
    }
  }, [dragging, handleMouseMove, handleMouseUp]);

  const handleScroll = useCallback(() => {
    if (laneRef.current && onScrollChange) {
      onScrollChange(laneRef.current.scrollLeft);
    }
  }, [onScrollChange]);

  useEffect(() => {
    if (laneRef.current && scrollLeft !== undefined) {
      laneRef.current.scrollLeft = scrollLeft;
    }
  }, [scrollLeft]);

  return (
    <div
      className="velocity-lane"
      ref={laneRef}
      onScroll={handleScroll}
    >
      <div className="velocity-lane__content">
        {notes.map((note, i) => {
          const x = getBarX(note, 0);
          const h = getBarHeight(note.velocity);
          const isSelected = selectedNoteIds.has(note.noteId);
          const isDragging = dragging?.noteId === note.noteId;
          return (
            <div
              key={note.noteId}
              className={`velocity-bar${isSelected ? " velocity-bar--selected" : ""}${isDragging ? " velocity-bar--dragging" : ""}`}
              style={{
                left: x,
                bottom: 2,
                width: barWidth,
                height: h,
              }}
              onMouseDown={(e) => handleMouseDown(e, note)}
              title={`Velocity: ${note.velocity}`}
            />
          );
        })}
      </div>
    </div>
  );
}
