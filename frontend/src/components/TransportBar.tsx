import { useState, useEffect, useRef, useCallback } from "react";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./TransportBar.css";

export default function TransportBar() {
  const transport = useTransportStore((s) => s.transport);
  const { snapEnabled, snapDivision, setSnapEnabled, setSnapDivision } = useUiStore();
  const isDirty = useProjectStore((s) => s.isDirty);
  const [bpmInput, setBpmInput] = useState<string | null>(null);

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

  // BPM tap state
  const tapTimesRef = useRef<number[]>([]);
  const handleTapBpm = useCallback(() => {
    const now = performance.now();
    const taps = tapTimesRef.current;
    taps.push(now);
    if (taps.length > 8) taps.shift();
    if (taps.length >= 2) {
      const intervals: number[] = [];
      for (let i = 1; i < taps.length; i++) intervals.push(taps[i] - taps[i - 1]);
      const avgMs = intervals.reduce((a, b) => a + b, 0) / intervals.length;
      const bpm = Math.round(60000 / avgMs);
      if (bpm >= 20 && bpm <= 999) {
        rpc.call("project.setTempo", { bpm }).catch(() => {});
      }
    }
  }, []);

  // Poll transport + dirty flag on interval
  useEffect(() => {
    const tick = () => {
      const state = useTransportStore.getState();
      const pstore = useProjectStore.getState();
      if (state.transport.isPlaying || state.transport.isRecording) {
        pstore.syncSnapshot(rpc);
      }
      pstore.syncDirtyFlag(rpc);
    };
    const interval = setInterval(tick, 250);
    return () => clearInterval(interval);
  }, []);

  const fmtTime = (sec: number) => {
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m}:${s.toString().padStart(2, "0")}`;
  };

  const barBeat = (() => {
    const totalBeats = transport.currentTimeSeconds * (transport.bpm / 60);
    const bar = Math.floor(totalBeats / 4) + 1;
    const beat = Math.floor(totalBeats % 4) + 1;
    return `${bar}.${beat}`;
  })();

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
        <span className="tb-bar-beat">{barBeat}</span>
        <span className="tb-time">
          {isDirty && <span className="tb-dirty" title="Project has unsaved changes">●</span>}
          {fmtTime(transport.currentTimeSeconds)}
        </span>
        <button className="tb-bpm-btn" onClick={handleTapBpm} title="Tap tempo">♩</button>
        <span
          className="tb-bpm"
          onDoubleClick={() => setBpmInput(transport.bpm.toFixed(1))}
        >
          {bpmInput != null ? (
            <input
              className="tb-bpm-input"
              autoFocus
              value={bpmInput}
              onChange={(e) => setBpmInput(e.target.value)}
              onBlur={() => { rpc.call("project.setTempo", { bpm: parseFloat(bpmInput) || 120 }).catch(() => {}); setBpmInput(null); }}
              onKeyDown={(e) => { if (e.key === "Enter") { (e.target as HTMLElement).blur(); } if (e.key === "Escape") setBpmInput(null); }}
            />
          ) : (
            `${transport.bpm.toFixed(1)} BPM`
          )}
        </span>
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
