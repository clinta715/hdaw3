import { useRef, useCallback } from "react";
import { rpc } from "../rpc";
import "./CCLane.css";

interface CCLaneProps {
  clipId: number;
  controllerNumber: number;
  width: number;
  pixelsPerBeat: number;
  scrollX: number;
}

export default function CCLane({ clipId, controllerNumber, width, pixelsPerBeat, scrollX }: CCLaneProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left + scrollX;
    const beat = x / pixelsPerBeat;
    const value = Math.round(127 * (1 - (e.clientY - rect.top) / rect.height));
    rpc.call("project.addCcPoint", {
      clipId,
      controllerNumber,
      beat,
      value: Math.max(0, Math.min(127, value)),
    });
  }, [clipId, controllerNumber, pixelsPerBeat, scrollX]);

  return (
    <div className="cc-lane">
      <div className="cc-label">CC{controllerNumber}</div>
      <canvas
        ref={canvasRef}
        width={width}
        height={60}
        className="cc-canvas"
        onClick={handleCanvasClick}
      />
    </div>
  );
}
