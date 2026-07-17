import { TrackSnapshot, MeterLevels } from "../rpc/types";
import "./MixerStrip.css";

interface Props {
  track: TrackSnapshot;
  meter: MeterLevels;
  isMaster?: boolean;
}

export default function MixerStrip({ track, meter, isMaster }: Props) {
  const pctL = Math.min(100, Math.max(0, meter.l * 100));
  const pctR = Math.min(100, Math.max(0, meter.r * 100));

  return (
    <div className={`mixer-strip ${isMaster ? "mixer-strip--master" : ""}`}>
      <div className="ms-color" style={{ background: track.color }} />
      <div className="ms-vu">
        <MeterBar value={pctL} />
        <MeterBar value={pctR} />
      </div>
      <div className="ms-name">{track.name}</div>
      <div className="ms-controls">
        <div className="ms-volume">
          {(track.volume * 100).toFixed(0)}%
        </div>
        <div className="ms-pan">
          {track.pan === 0 ? "C" : track.pan < 0 ? `L${Math.abs(Math.round(track.pan * 100))}` : `R${Math.round(track.pan * 100)}`}
        </div>
      </div>
    </div>
  );
}

function MeterBar({ value }: { value: number }) {
  let cls = "ms-fill";
  if (value > 90) cls += " ms-clip";
  else if (value > 75) cls += " ms-hot";
  return (
    <div className="ms-meter">
      <div className={cls} style={{ height: `${value}%` }} />
    </div>
  );
}
