import { useState } from "react";
import { TrackSnapshot, MeterLevels } from "../rpc/types";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import "./MixerStrip.css";

interface Props {
  track: TrackSnapshot;
  meter: MeterLevels;
  isMaster?: boolean;
}

export default function MixerStrip({ track, meter, isMaster }: Props) {
  const [volume, setVolume] = useState(track.volume);
  const [pan, setPan] = useState(track.pan);

  const pctL = Math.min(100, Math.max(0, meter.l * 100));
  const pctR = Math.min(100, Math.max(0, meter.r * 100));

  const commitVolume = () => {
    if (volume !== track.volume)
      rpc.call("project.setTrackVolume", { trackIndex: track.index, volume }).catch(console.error);
  };
  const commitPan = () => {
    if (pan !== track.pan)
      rpc.call("project.setTrackPan", { trackIndex: track.index, pan }).catch(console.error);
  };

  return (
    <div className={`mixer-strip ${isMaster ? "mixer-strip--master" : ""}`}>
      <div className="ms-color" style={{ background: colorStr(track.color) }} />
      <div className="ms-vu">
        <MeterBar value={pctL} />
        <MeterBar value={pctR} />
      </div>
      <div className="ms-name">{track.name}</div>
      <div className="ms-controls">
        <input
          type="range"
          className="ms-fader"
          min={0}
          max={1}
          step={0.01}
          value={volume}
          onChange={(e) => setVolume(parseFloat(e.target.value))}
          onMouseUp={commitVolume}
          onBlur={commitVolume}
        />
        <input
          type="range"
          className="ms-pan-fader"
          min={-1}
          max={1}
          step={0.01}
          value={pan}
          onChange={(e) => setPan(parseFloat(e.target.value))}
          onMouseUp={commitPan}
          onBlur={commitPan}
        />
      </div>
      {!isMaster && (
        <div className="ms-buttons">
          <button
            className={`ms-btn${track.muted ? " active" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackMuted", { trackIndex: track.index, muted: !track.muted }).catch(console.error);
            }}
            title="Mute"
          >M</button>
          <button
            className={`ms-btn${track.soloed ? " active" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackSoloed", { trackIndex: track.index, soloed: !track.soloed }).catch(console.error);
            }}
            title="Solo"
          >S</button>
          <button
            className={`ms-btn${track.armed ? " active" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackArmed", { trackIndex: track.index, armed: !track.armed }).catch(console.error);
            }}
            title="Arm"
          >R</button>
        </div>
      )}
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
