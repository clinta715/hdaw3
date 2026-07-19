import { useEffect, useRef } from "react";
import { useKeyboardShortcuts } from "./hooks/useKeyboardShortcuts";
import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import ClipEditor from "./components/ClipEditor";
import AudioClipEditor from "./components/AudioClipEditor";
import StepSequencer from "./components/StepSequencer";
import AutomationPanel from "./components/AutomationPanel";
import FXChain from "./components/FXChain";
import ModulationPanel from "./components/ModulationPanel";
import BottomTabs from "./components/BottomTabs";
import StatusBar from "./components/StatusBar";
import FileBrowser from "./components/FileBrowser";
import { useUiStore } from "./store/uiStore";
import { useProjectStore } from "./store/projectStore";
import { useBrowserStore } from "./store/browserStore";
import { rpc } from "./rpc";

function App() {
  useKeyboardShortcuts();
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const activeBottomTab = useUiStore((s) => s.activeBottomTab);
  const setActiveBottomTab = useUiStore((s) => s.setActiveBottomTab);
  const snapshot = useProjectStore((s) => s.snapshot);
  const prevTabRef = useRef(activeBottomTab);
  const browserVisible = useBrowserStore((s) => s.visible);
  const toggleBrowser = useBrowserStore((s) => s.toggleVisible);

  // Auto-switch bottom tab when a single clip is selected
  useEffect(() => {
    if (selectedClipIds.size === 1) {
      const id = selectedClipIds.values().next().value;
      const clip = snapshot?.clips.find((c) => c.clipId === id);
      if (clip) {
        prevTabRef.current = useUiStore.getState().activeBottomTab;
        setActiveBottomTab(clip.isMidi ? "piano-roll" : "audio-editor");
      }
    }
  }, [selectedClipIds, snapshot, setActiveBottomTab]);

  // When selection clears and we're on a clip-specific tab, restore previous tab
  useEffect(() => {
    const isClipTab = activeBottomTab === "audio-editor" || activeBottomTab === "piano-roll";
    if (isClipTab && selectedClipIds.size !== 1) {
      const restored = prevTabRef.current === "audio-editor" || prevTabRef.current === "piano-roll"
        ? "mixer" : prevTabRef.current;
      setActiveBottomTab(restored);
    }
  }, [selectedClipIds, activeBottomTab, setActiveBottomTab]);

  const bottomTabs = [
    { id: "mixer", label: "Mixer", content: <Mixer /> },
    { id: "piano-roll", label: "Piano Roll", content: <PianoRoll /> },
    { id: "automation", label: "Automation", content: <AutomationPanel rpc={rpc} /> },
    { id: "fx", label: "FX Chain", content: <FXChain /> },
    { id: "audio-editor", label: "Audio Editor", content: <AudioClipEditor /> },
    { id: "modulation", label: "Modulation", content: <ModulationPanel /> },
    { id: "step-seq", label: "Step Seq", content: <StepSequencer /> },
  ];

  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <button
        className="browser-toggle-btn"
        onClick={toggleBrowser}
        title="Toggle File Browser (Ctrl+B)"
        style={{
          position: "fixed",
          top: 10,
          right: 10,
          zIndex: 100,
          background: browserVisible ? "#2a3a4a" : "#222",
          border: "1px solid #555",
          color: browserVisible ? "#fff" : "#aaa",
          padding: "5px 10px",
          borderRadius: "4px",
          cursor: "pointer",
          fontSize: "14px",
        }}
      >
        📁
      </button>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline" style={{ display: "flex", flexDirection: "row" }}>
        <TimelineMinimal />
        <FileBrowser />
      </main>
      {useUiStore((s) => s.selectedClipIds.size === 1) && (
        <div className="clip-editor-container">
          <ClipEditor />
        </div>
      )}
      <footer className="bottom-panel">
        <BottomTabs
          tabs={bottomTabs}
          defaultTab="mixer"
          activeTab={activeBottomTab}
          onTabChange={setActiveBottomTab}
        />
      </footer>
      <StatusBar />
    </div>
  );
}

export default App;
