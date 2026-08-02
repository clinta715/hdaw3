import { useState, useEffect, useRef, useCallback } from "react";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { useTransportExtrasStore } from "../store/transportExtrasStore";
import { useBrowserStore } from "../store/browserStore";
import { rpc } from "../rpc";
import { reportRpcError } from "../store/notifyStore";
import FileMenu from "./FileMenu";
import PluginManagerDialog from "./PluginManagerDialog";
import PreferencesDialog from "./PreferencesDialog";
import PhraseGeneratorDialog from "./PhraseGeneratorDialog";
import { FolderIcon, KnobIcon, NoteIcon, SlidersIcon } from "./Icons";
import "./TransportBar.css";

export default function TransportBar() {
  const transport = useTransportStore((s) => s.transport);
  const snapEnabled = useUiStore((s) => s.snapEnabled);
  const snapDivision = useUiStore((s) => s.snapDivision);
  const setSnapEnabled = useUiStore((s) => s.setSnapEnabled);
  const setSnapDivision = useUiStore((s) => s.setSnapDivision);
  const snapGridOffset = useUiStore((s) => s.snapGridOffset);
  const snapToEvents = useUiStore((s) => s.snapToEvents);
  const setSnapGridOffset = useUiStore((s) => s.setSnapGridOffset);
  const setSnapToEvents = useUiStore((s) => s.setSnapToEvents);
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
  const [ccRecArmed, setCcRecArmed] = useState(false);
  const [midiRecArmed, setMidiRecArmed] = useState(false);
  const showPhraseGenerator = useUiStore((s) => s.showPhraseGenerator);
  const setShowPhraseGenerator = useUiStore((s) => s.setShowPhraseGenerator);
  const browserVisible = useBrowserStore((s) => s.visible);
  const toggleBrowser = useBrowserStore((s) => s.toggleVisible);
  const viewMode = useUiStore((s) => s.viewMode);
  const setViewMode = useUiStore((s) => s.setViewMode);

  const cmd = (method: string) => () => {
    rpc.call(method).catch(console.error);
  };

  const handleUndo = async () => {
    await rpc.call("project.undo").catch((err) => reportRpcError("project.undo", err));
    // Undo replays an inverse mutation on the ValueTree; the debounced
    // notify.treeChanged push reconciles the snapshot. Mark dirty optimistically.
    useProjectStore.setState({ isDirty: true });
  };

  const handleRedo = async () => {
    await rpc.call("project.redo").catch((err) => reportRpcError("project.redo", err));
    useProjectStore.setState({ isDirty: true });
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
        rpc.call("project.setTempo", { bpm }).catch((err) => reportRpcError("project.setTempo", err));
      }
    }
  }, []);

  // Poll the dirty flag on an interval. Snapshot updates arrive via the
  // notify.treeChanged push (debounced to ~16 ms in FrontendTreeWatcher), so
  // we don't poll the snapshot here anymore — that was redundant load on
  // the engine and produced ~4 snapshot re-fetches per second on top of the
  // push-driven ones. The dirty flag is not pushed, so it still needs polling.
  useEffect(() => {
    const tick = () => {
      useProjectStore.getState().syncDirtyFlag(rpc);
    };
    const interval = setInterval(tick, 1000);
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
      {/* File cluster — pinned to the far left */}
      <div className="tb-group tb-group--file">
        <FileMenu />
        <button
          className={`tb-icon-btn browser-toggle-btn${browserVisible ? " browser-toggle-btn--active" : ""}`}
          onClick={toggleBrowser}
          title="Toggle File Browser (Ctrl+B)"
        >
          <FolderIcon />
        </button>
        <button
          className={`tb-btn${viewMode === "session" ? " active" : ""}`}
          onClick={() => setViewMode(viewMode === "arrange" ? "session" : "arrange")}
          title="Toggle Session/Arrangement View (Tab)"
        >
          {viewMode === "session" ? "Sess" : "Arr"}
        </button>
      </div>

      {/* Core transport */}
      <div className="tb-group">
        <button className="tb-btn" onClick={cmd("transport.rewind")} title="Rewind">⏮</button>
        <button
          className={`tb-btn tb-play ${transport.isPlaying ? "active" : ""}`}
          onClick={cmd(transport.isPlaying ? "transport.pause" : "transport.play")}
          title={transport.isPlaying ? "Pause" : "Play"}
        >
          {transport.isPlaying ? "⏸" : "▶"}
        </button>
        <button className="tb-btn" onClick={cmd("transport.stop")} title="Stop">⏹</button>
        <button
          className={`tb-btn ${transport.isRecording ? "recording" : ""}`}
          onClick={cmd("transport.record")}
          title="Record"
        >
          ●
        </button>
      </div>

      <div className="tb-sep" />

      {/* Record arm */}
      <div className="tb-group">
        <button
          className={`tb-btn ${ccRecArmed ? "recording" : ""}`}
          onClick={() => {
            const next = !ccRecArmed;
            setCcRecArmed(next);
            rpc.call("project.setCcRecordArmed", { armed: next }).catch((err) => reportRpcError("project.setCcRecordArmed", err));
          }}
          title="Record MIDI CC automation"
        >
          CC
        </button>
        <button
          className={`tb-btn ${midiRecArmed ? "recording" : ""}`}
          onClick={() => {
            const next = !midiRecArmed;
            setMidiRecArmed(next);
            rpc.call("project.setMidiNoteRecordArmed", { armed: next }).catch((err) => reportRpcError("project.setMidiNoteRecordArmed", err));
          }}
          title="Record MIDI notes (armed tracks)"
        >
          MI
        </button>
      </div>

      <div className="tb-sep" />

      {/* LCD display — the primary readout */}
      <div className="tb-display">
        <div className="tb-display-top">
          <span className="tb-bar-beat">{barBeat}</span>
          <span className="tb-time">
            {isDirty && <span className="tb-dirty" title="Project has unsaved changes">●</span>}
            {fmtTime(transport.currentTimeSeconds)}
          </span>
        </div>
        <div className="tb-display-bottom">
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
                onBlur={() => { rpc.call("project.setTempo", { bpm: parseFloat(bpmInput) || 120 }).catch((err) => reportRpcError("project.setTempo", err)); setBpmInput(null); }}
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
      </div>

      <div className="tb-sep" />

      {/* Snap */}
      <div className="tb-group">
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
        <button
          className={`tb-snap-btn ${snapGridOffset ? "active" : ""}`}
          onClick={() => setSnapGridOffset(!snapGridOffset)}
          title="Preserve offset when moving"
        >Off</button>
        <button
          className={`tb-snap-btn ${snapToEvents ? "active" : ""}`}
          onClick={() => setSnapToEvents(!snapToEvents)}
          title="Snap to clip & note edges"
        >Evt</button>
      </div>

      <div className="tb-sep" />

      {/* Edit */}
      <div className="tb-group">
        <button className="tb-btn" onClick={handleUndo} title="Undo (Ctrl+Z)">↩</button>
        <button className="tb-btn" onClick={handleRedo} title="Redo (Ctrl+Shift+Z)">↪</button>
      </div>

      <div className="tb-sep" />

      {/* Transport options — compact text toggles */}
      <div className="tb-group">
        <button
          className={`tb-toggle ${transport.isLooping ? "active" : ""}`}
          onClick={cmd("transport.toggleLoop")}
          title="Toggle Loop"
        >
          Loop
        </button>
        <button
          className={`tb-toggle ${transport.punchEnabled ? "active" : ""}`}
          onClick={() => rpc.call("transport.setPunchEnabled", { enabled: !transport.punchEnabled })}
          title="Punch In/Out (uses loop region)"
        >
          Punch
        </button>
        <button
          className={`tb-toggle ${metronomeEnabled ? "active" : ""}`}
          onClick={() => {
            const next = !metronomeEnabled;
            setExtras({ metronomeEnabled: next });
            rpc.call("project.setMetronomeEnabled", { enabled: next }).catch((err) => reportRpcError("project.setMetronomeEnabled", err));
          }}
          title="Metronome"
        >
          Met
        </button>
        <button
          className={`tb-toggle ${countInEnabled ? "active" : ""}`}
          onClick={() => setExtras({ countInEnabled: !countInEnabled })}
          title="Count-in (1 bar)"
        >
          1Bar
        </button>
        <button
          className={`tb-toggle ${followPlayhead ? "active" : ""}`}
          onClick={() => setExtras({ followPlayhead: !followPlayhead })}
          title="Follow Playhead"
        >
          Follow
        </button>
      </div>

      {/* Window tools — pinned to the far right, monochrome icons */}
      <div className="tb-group tb-group--tools">
        <button className="tb-icon-btn" onClick={() => setShowPluginManager(true)} title="Plugin Manager"><KnobIcon /></button>
        <button className="tb-icon-btn" onClick={() => setShowPhraseGenerator(true)} title="Phrase Generator (Ctrl+Shift+G)"><NoteIcon /></button>
        <button className="tb-icon-btn" onClick={() => setShowPrefs(true)} title="Preferences"><SlidersIcon /></button>
      </div>
      {showPluginManager && <PluginManagerDialog onClose={() => setShowPluginManager(false)} />}
      {showPrefs && <PreferencesDialog onClose={() => setShowPrefs(false)} />}
      {showPhraseGenerator && <PhraseGeneratorDialog onClose={() => setShowPhraseGenerator(false)} />}
    </div>
  );
}
