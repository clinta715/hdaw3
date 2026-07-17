import "./App.css";
import TransportBar from "./components/TransportBar";

function App() {
  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <aside className="track-headers" />
      <main className="timeline" />
      <footer className="bottom-panel">
        <section className="mixer" />
        <section className="piano-roll" />
      </footer>
    </div>
  );
}

export default App;
