import React, { useCallback, useRef, useState } from "react";
import { useArrangerStore, ArrangerRegionSnapshot } from "../store/arrangerStore";
import { rpc } from "../rpc";
import "./ArrangerLane.css";

interface Props {
  pps: number;
  scrollLeft: number;
}

export const ArrangerLane: React.FC<Props> = ({ pps, scrollLeft }) => {
  const regions = useArrangerStore((s) => s.regions);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [dragState, setDragState] = useState<{
    type: "move" | "resize-left" | "resize-right";
    regionID: string;
    startX: number;
    origStart: number;
    origDuration: number;
  } | null>(null);
  const laneRef = useRef<HTMLDivElement>(null);

  const handleMouseDown = useCallback(
    (e: React.MouseEvent, region: ArrangerRegionSnapshot, handle?: "left" | "right") => {
      e.stopPropagation();
      setSelectedId(region.regionID);
      setDragState({
        type: handle === "left" ? "resize-left" : handle === "right" ? "resize-right" : "move",
        regionID: region.regionID,
        startX: e.clientX,
        origStart: region.startTime,
        origDuration: region.duration,
      });
    },
    []
  );

  const handleMouseMove = useCallback(
    (e: React.MouseEvent) => {
      if (!dragState) return;
      const dx = e.clientX - dragState.startX;
      const dBeats = dx / pps;

      let newStart = dragState.origStart;
      let newDuration = dragState.origDuration;

      if (dragState.type === "move") {
        newStart = Math.max(0, dragState.origStart + dBeats);
      } else if (dragState.type === "resize-left") {
        newStart = Math.max(0, dragState.origStart + dBeats);
        newDuration = Math.max(1, dragState.origDuration - dBeats);
      } else {
        newDuration = Math.max(1, dragState.origDuration + dBeats);
      }

      newStart = Math.round(newStart * 4) / 4;
      newDuration = Math.round(newDuration * 4) / 4;
      if (newDuration < 0.25) newDuration = 0.25;

      rpc.call("project.setArrangerRegionBounds", {
        regionID: dragState.regionID,
        startTime: newStart,
        duration: newDuration,
      });
    },
    [dragState, pps]
  );

  const handleMouseUp = useCallback(() => {
    setDragState(null);
  }, []);

  const handleDoubleClick = useCallback(
    (e: React.MouseEvent, region: ArrangerRegionSnapshot) => {
      e.stopPropagation();
      setEditingId(region.regionID);
    },
    []
  );

  const handleRename = useCallback(
    (regionID: string, newName: string) => {
      if (newName.trim()) {
        rpc.call("project.setArrangerRegionName", { regionID, name: newName.trim() });
      }
      setEditingId(null);
    },
    []
  );

  const handleDelete = useCallback(() => {
    if (selectedId) {
      rpc.call("project.removeArrangerRegion", { regionID: selectedId });
      setSelectedId(null);
    }
  }, [selectedId]);

  const handleLaneClick = useCallback(
    (e: React.MouseEvent) => {
      if (e.target === laneRef.current) {
        setSelectedId(null);
      }
    },
    []
  );

  const handleLaneDoubleClick = useCallback(
    (e: React.MouseEvent) => {
      if (!laneRef.current) return;
      const rect = laneRef.current.getBoundingClientRect();
      const x = e.clientX - rect.left + scrollLeft;
      const startBeat = Math.round((x / pps) * 4) / 4;
      const dur = 8;
      rpc.call("project.addArrangerRegion", {
        name: `Section ${regions.length + 1}`,
        startTime: startBeat,
        duration: dur,
      });
    },
    [pps, scrollLeft, regions.length]
  );

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === "Delete" || e.key === "Backspace") {
        handleDelete();
      }
    },
    [handleDelete]
  );

  if (regions.length === 0 && !dragState) {
    return (
      <div
        className="tl-arranger-lane tl-arranger-lane-empty"
        onDoubleClick={handleLaneDoubleClick}
        ref={laneRef}
      />
    );
  }

  return (
    <div
      className="tl-arranger-lane"
      ref={laneRef}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onClick={handleLaneClick}
      onDoubleClick={handleLaneDoubleClick}
      onKeyDown={handleKeyDown}
      tabIndex={0}
    >
      {regions.map((region) => {
        const left = region.startTime * pps - scrollLeft;
        const width = region.duration * pps;
        const isSelected = region.regionID === selectedId;
        const isEditing = region.regionID === editingId;
        const bgColor = region.color
          ? `#${region.color.toString(16).padStart(8, "0").slice(2)}`
          : "var(--accent)";

        return (
          <div
            key={region.regionID}
            className={`tl-arranger-region${isSelected ? " selected" : ""}`}
            style={{ left, width, backgroundColor: bgColor }}
            onMouseDown={(e) => handleMouseDown(e, region)}
            onDoubleClick={(e) => handleDoubleClick(e, region)}
          >
            <div
              className="tl-arranger-region-handle tl-arranger-region-handle-left"
              onMouseDown={(e) => handleMouseDown(e, region, "left")}
            />
            {isEditing ? (
              <input
                autoFocus
                defaultValue={region.name}
                onBlur={(e) => handleRename(region.regionID, e.currentTarget.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") handleRename(region.regionID, e.currentTarget.value);
                  if (e.key === "Escape") setEditingId(null);
                }}
                onClick={(e) => e.stopPropagation()}
                style={{
                  background: "transparent",
                  border: "none",
                  color: "var(--text-primary)",
                  fontSize: "11px",
                  fontWeight: 500,
                  width: "100%",
                  outline: "none",
                }}
              />
            ) : (
              <span className="tl-arranger-region-label">{region.name}</span>
            )}
            <div
              className="tl-arranger-region-handle tl-arranger-region-handle-right"
              onMouseDown={(e) => handleMouseDown(e, region, "right")}
            />
          </div>
        );
      })}
    </div>
  );
};
