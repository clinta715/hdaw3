import { useState, useEffect } from "react";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./TransportBar.css";

export default function TransportBar() {
  const transport = useTransportStore((s) => s.transport);
  const { snapEnabled, snapDivision, setSnapEnabled, setSnapDivision } = useUiStore();
  const isDirty = useProjectStore((s) => s.isDirty);

  const cmd = (method: string) => () => {
    rpc.call(method).catch(console.error);
  };

  const handleUndo = async () => {
    await rpc.call("project.undo").catch(() => {});
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  };

  const handleRedo = async () => {
    await rpc.call("project.redo").catch(() => {});
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  };

  useEffect(() => {
    const check = () => {
      useProjectStore.getState().syncDirtyFlag(rpc);
    };
    const interval = setInterval(check, 2000);
    check();
    return () => clearInterval(interval);
  }, []);

  const fmtTime = (sec: number) => {
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m}:${s.toString().padStart(2, "0")}`;
  };

  return (
    <div className="transport-bar">
      <div className="transport-left">
        <button className="tb-btn" onClick={cmd("transport.rewind")} title="Rewind">⏮</button>
        <button
          className={`tb-btn tb-play ${transport.isPlaying ? "active" : ""}`}
          onClick={cmd(transport.isPlaying ? "transport.pause" : "transport.play")}
          title={transport.isPlaying ? "Pause" : "Play"}
        >
          {transport.isPlaying ? "⏸" : "▶"}
        </button>
        <button
          className={`tb-btn ${transport.isRecording ? "recording" : ""}`}
          onClick={cmd(transport.isRecording ? "transport.stop" : "transport.record")}
          title="Record"
        >
          ●
        </button>
        <button className="tb-btn" onClick={cmd("transport.stop")} title="Stop">⏹</button>
      </div>
      <div className="transport-center">
        <span className="tb-time">
          {isDirty && <span className="tb-dirty" title="Project has unsaved changes">●</span>}
          {fmtTime(transport.currentTimeSeconds)}
        </span>
        <span className="tb-bpm">{transport.bpm.toFixed(1)} BPM</span>
      </div>
      <div className="transport-snap">
        <button
          className={`tb-snap-btn ${snapEnabled ? "active" : ""}`}
          onClick={() => setSnapEnabled(!snapEnabled)}
          title="Toggle Snap"
        >Snap</button>
        <select
          className="tb-snap-select"
          value={snapDivision}
          onChange={(e) => setSnapDivision(Number(e.target.value))}
        >
          <option value={0}>Bar</option>
          <option value={1}>Beat</option>
          <option value={2}>1/8</option>
          <option value={3}>1/16</option>
          <option value={4}>1/32</option>
        </select>
      </div>
      <div className="transport-right">
        <div className="tb-undo">
          <button className="tb-btn" onClick={handleUndo} title="Undo (Ctrl+Z)">↩</button>
          <button className="tb-btn" onClick={handleRedo} title="Redo (Ctrl+Shift+Z)">↪</button>
        </div>
        <button
          className={`tb-btn ${transport.isLooping ? "active" : ""}`}
          onClick={cmd("transport.toggleLoop")}
          title="Toggle Loop"
        >
          🔁
        </button>
      </div>
    </div>
  );
}
