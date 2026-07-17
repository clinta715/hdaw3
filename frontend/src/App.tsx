import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";

function App() {
  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline" />
      <footer className="bottom-panel">
        <section className="mixer">
          <Mixer />
        </section>
        <section className="piano-roll" />
      </footer>
    </div>
  );
}

export default App;
