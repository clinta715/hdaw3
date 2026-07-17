import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";

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
