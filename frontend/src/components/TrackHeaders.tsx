import { useProjectStore } from "../store/projectStore";
import "./TrackHeaders.css";

export default function TrackHeaders() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];

  return (
    <div className="track-headers">
      <div className="th-title">Tracks</div>
      {tracks.length === 0 && (
        <div className="th-empty">No tracks loaded</div>
      )}
      {tracks.map((track) => (
        <div key={track.index} className="th-row">
          <div className="th-color" style={{ background: track.color }} />
          <div className="th-info">
            <div className="th-name">{track.name}</div>
            <div className="th-type">{track.type}</div>
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
