import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import ClipEditor from "./components/ClipEditor";
import { useUiStore } from "./store/uiStore";

function App() {
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
        <section className="mixer">
          <Mixer />
        </section>
        <section className="piano-roll">
          <PianoRoll />
        </section>
      </footer>
    </div>
  );
}

export default App;
