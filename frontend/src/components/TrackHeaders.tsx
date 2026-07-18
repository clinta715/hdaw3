import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";
import "./TrackHeaders.css";

export default function TrackHeaders() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];

  const handleMute = (idx: number, muted: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackMuted", { trackIndex: idx, muted: !muted }).catch(console.error);
  };

  const handleSolo = (idx: number, soloed: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackSoloed", { trackIndex: idx, soloed: !soloed }).catch(console.error);
  };

  const handleArm = (idx: number, armed: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackArmed", { trackIndex: idx, armed: !armed }).catch(console.error);
  };

  function colorStr(c: number): string {
    return "#" + c.toString(16).padStart(6, "0");
  }

  const formatPan = (pan: number): string => {
    if (pan === 0) return "C";
    const pct = Math.round(Math.abs(pan) * 100);
    return pan < 0 ? `L${pct}` : `R${pct}`;
  };

  return (
    <div className="track-headers">
      <div className="th-title">Tracks</div>
      {tracks.length === 0 && (
        <div className="th-empty">No tracks loaded</div>
      )}
      {tracks.map((track) => (
        <div key={track.index} className="th-row">
          <div className="th-color" style={{ background: colorStr(track.color) }} />
          <div className="th-info">
            <div className="th-name">{track.name}</div>
            <div className="th-type">{track.type}</div>
          </div>
          {track.type !== "master" && (
            <div className="th-controls">
              <button
                className={`th-btn th-mute${track.muted ? " active" : ""}`}
                onClick={(e) => handleMute(track.index, track.muted, e)}
                title="Mute"
              >
                M
              </button>
              <button
                className={`th-btn th-solo${track.soloed ? " active" : ""}`}
                onClick={(e) => handleSolo(track.index, track.soloed, e)}
                title="Solo"
              >
                S
              </button>
              <button
                className={`th-btn th-arm${track.armed ? " active" : ""}`}
                onClick={(e) => handleArm(track.index, track.armed, e)}
                title="Arm"
              >
                R
              </button>
            </div>
          )}
          <div className="th-values">
            <span className="th-vol">V:{Math.round(track.volume * 100)}%</span>
            <span className="th-pan">{formatPan(track.pan)}</span>
          </div>
          <div className="th-meters">
            <MeterBar value={track.meterL} />
            <MeterBar value={track.meterR} />
          </div>
        </div>
      ))}
    </div>
  );
}

function MeterBar({ value }: { value: number }) {
  const pct = Math.min(100, Math.max(0, value * 100));
  let cls = "meter-fill";
  if (pct > 90) cls += " meter-clip";
  else if (pct > 75) cls += " meter-hot";
  return (
    <div className="meter-track">
      <div className={cls} style={{ height: `${pct}%` }} />
    </div>
  );
}
