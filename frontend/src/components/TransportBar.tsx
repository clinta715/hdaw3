import { useState, useEffect, useRef, useCallback } from "react";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useTransportExtrasStore } from "../store/transportExtrasStore";
import { useBrowserStore } from "../store/browserStore";
import { rpc } from "../rpc";
import FileMenu from "./FileMenu";
import PluginManagerDialog from "./PluginManagerDialog";
import PreferencesDialog from "./PreferencesDialog";
import "./TransportBar.css";

export default function TransportBar() {
  const transport = useTransportStore((s) => s.transport);
  const { snapEnabled, snapDivision, setSnapEnabled, setSnapDivision } = useUiStore();
  const isDirty = useProjectStore((s) => s.isDirty);
  const metronomeEnabled = useTransportExtrasStore((s) => s.metronomeEnabled);
  const countInEnabled = useTransportExtrasStore((s) => s.countInEnabled);
  const followPlayhead = useTransportExtrasStore((s) => s.followPlayhead);
  const timeSignatureNum = useTransportExtrasStore((s) => s.timeSignatureNum);
  const timeSignatureDen = useTransportExtrasStore((s) => s.timeSignatureDen);
  const setExtras = useTransportExtrasStore((s) => s.set);
  const [bpmInput, setBpmInput] = useState<string | null>(null);
  const [showPluginManager, setShowPluginManager] = useState(false);
  const [showPrefs, setShowPrefs] = useState(false);
  const browserVisible = useBrowserStore((s) => s.visible);
  const toggleBrowser = useBrowserStore((s) => s.toggleVisible);

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

  const handleAddTrack = () => {
    rpc.call("project.addTrack").catch(() => {});
  };
  const handleRemoveTrack = () => {
    const idx = useUiStore.getState().selectedTrackIndex;
    if (idx != null) {
      rpc.call("project.removeTrack", { trackIndex: idx }).catch(() => {});
    }
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
    const bar = Math.floor(totalBeats / timeSignatureNum) + 1;
    const beat = Math.floor(totalBeats % timeSignatureNum) + 1;
    return `${bar}.${beat}`;
  })();

  return (
    <div className="transport-bar">
      <div className="transport-left">
        <FileMenu />
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
        <span className="tb-time-sig">
          {timeSignatureNum}/{timeSignatureDen}
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
      <div className="transport-track-ops">
        <button className="tb-btn" onClick={handleAddTrack} title="Add Track">+T</button>
        <button className="tb-btn" onClick={handleRemoveTrack} title="Remove Track">-T</button>
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
        <button
          className={`tb-btn ${metronomeEnabled ? "active" : ""}`}
          onClick={() => {
            const next = !metronomeEnabled;
            setExtras({ metronomeEnabled: next });
            rpc.call("project.setMetronomeEnabled", { enabled: next }).catch(() => {});
          }}
          title="Metronome"
        >
          ♪
        </button>
        <button
          className={`tb-btn ${countInEnabled ? "active" : ""}`}
          onClick={() => setExtras({ countInEnabled: !countInEnabled })}
          title="Count-in (1 bar)"
        >
          1Bar
        </button>
        <button
          className={`tb-btn ${followPlayhead ? "active" : ""}`}
          onClick={() => setExtras({ followPlayhead: !followPlayhead })}
          title="Follow Playhead"
        >
          →|
        </button>
        <button className="tb-btn" onClick={() => setShowPluginManager(true)} title="Plugin Manager">🎛️</button>
        <button className="tb-btn" onClick={() => setShowPrefs(true)} title="Preferences">⚙</button>
        <button
          className={`tb-btn ${browserVisible ? "active" : ""}`}
          onClick={toggleBrowser}
          title="Toggle File Browser (Ctrl+B)"
        >
          📁
        </button>
      </div>
      {showPluginManager && <PluginManagerDialog onClose={() => setShowPluginManager(false)} />}
      {showPrefs && <PreferencesDialog onClose={() => setShowPrefs(false)} />}
    </div>
  );
}
