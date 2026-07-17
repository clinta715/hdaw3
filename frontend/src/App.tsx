import "./App.css";

function App() {
  return (
    <div className="app-shell">
      <header className="transport-bar" />
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
