import { useTransportStore } from "../store/transportStore";
import { rpc } from "../rpc";
import "./TransportBar.css";

export default function TransportBar() {
  const transport = useTransportStore((s) => s.transport);

  const cmd = (method: string) => () => {
    rpc.call(method).catch(console.error);
  };

  const fmtTime = (sec: number) => {
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m}:${s.toString().padStart(2, "0")}`;
  };

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
        <span className="tb-time">{fmtTime(transport.currentTimeSeconds)}</span>
        <span className="tb-bpm">{transport.bpm.toFixed(1)} BPM</span>
      </div>
      <div className="transport-right">
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
