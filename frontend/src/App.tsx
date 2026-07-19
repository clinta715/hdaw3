import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import ClipEditor from "./components/ClipEditor";
import AutomationPanel from "./components/AutomationPanel";
import FXChain from "./components/FXChain";
import BottomTabs from "./components/BottomTabs";
import { useUiStore } from "./store/uiStore";
import { rpc } from "./rpc";

function App() {
  const bottomTabs = [
    { id: "mixer", label: "Mixer", content: <Mixer /> },
    { id: "piano-roll", label: "Piano Roll", content: <PianoRoll /> },
    { id: "automation", label: "Automation", content: <AutomationPanel rpc={rpc} /> },
    { id: "fx", label: "FX Chain", content: <FXChain /> },
  ];

  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline">
        <TimelineMinimal />
      </main>
      {useUiStore((s) => s.selectedClipId) != null && (
        <div className="clip-editor-container">
          <ClipEditor />
        </div>
      )}
      <footer className="bottom-panel">
        <BottomTabs tabs={bottomTabs} defaultTab="mixer" />
      </footer>
    </div>
  );
}

export default App;
