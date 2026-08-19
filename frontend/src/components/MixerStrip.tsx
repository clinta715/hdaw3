import { useState, useEffect } from "react";
import { TrackSnapshot, MeterLevels, SendSnapshot } from "../rpc/types";
import { rpc } from "../rpc";
import { useAutomationStore } from "../store/automationStore";
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
  const [sends, setSends] = useState<SendSnapshot[]>([]);

  useEffect(() => {
    rpc.call("read.getTrackSends", { trackIndex: track.index })
      .then((data: SendSnapshot[]) => setSends(data))
      .catch(() => {});
  }, [track.index]);

  const pctL = Math.min(100, Math.max(0, meter.l * 100));
  const pctR = Math.min(100, Math.max(0, meter.r * 100));
  const rmsPctL = Math.min(100, Math.max(0, (meter.rmsL ?? 0) * 100));
  const rmsPctR = Math.min(100, Math.max(0, (meter.rmsR ?? 0) * 100));
  const lufsVal = meter.lufs ?? -70;

  const commitVolume = () => {
    if (volume === track.volume) return;
    if (isMaster)
      rpc.call("project.setMasterGain", { gain: volume }).catch(console.error);
    else
      rpc.call("project.setTrackVolume", { trackIndex: track.index, volume }).catch(console.error);
  };
  const commitPan = () => {
    if (pan !== track.pan)
      rpc.call("project.setTrackPan", { trackIndex: track.index, pan }).catch(console.error);
  };

  const setSendLevel = (si: number, level: number) => {
    setSends((prev) => prev.map((s) => (s.sendIndex === si ? { ...s, level } : s)));
    rpc.call("project.setTrackSendLevel", { trackIndex: track.index, sendIndex: si, level }).catch(console.error);
  };

  const toggleSendMode = (si: number) => {
    const s = sends.find((x) => x.sendIndex === si);
    if (!s) return;
    const newMode = !s.isPreFader;
    setSends((prev) => prev.map((x) => (x.sendIndex === si ? { ...x, isPreFader: newMode } : x)));
    rpc.call("project.setTrackSendMode", { trackIndex: track.index, sendIndex: si, isPreFader: newMode }).catch(console.error);
  };

  const toggleSendBypass = (si: number) => {
    const s = sends.find((x) => x.sendIndex === si);
    if (!s) return;
    const newBypass = !s.bypassed;
    setSends((prev) => prev.map((x) => (x.sendIndex === si ? { ...x, bypassed: newBypass } : x)));
    rpc.call("project.setTrackSendBypassed", { trackIndex: track.index, sendIndex: si, bypassed: newBypass }).catch(console.error);
  };

  return (
    <div className={`mixer-strip ${isMaster ? "mixer-strip--master" : ""}`}>
      <div className="ms-color" style={{ background: colorStr(track.color) }} />
      <div className="ms-name">{track.name}</div>
      <div className="ms-fader-row">
        <div className="ms-vu">
          <MeterBar value={pctL} rmsValue={rmsPctL} />
          <MeterBar value={pctR} rmsValue={rmsPctR} />
        </div>
        <input
          type="range"
          className="ms-fader"
          min={0}
          max={1}
          step={0.01}
          value={volume}
          onChange={(e) => setVolume(parseFloat(e.target.value))}
          onMouseDown={() => useAutomationStore.getState().setLastClickedParamID(1)}
          onMouseUp={commitVolume}
          onBlur={commitVolume}
        />
      </div>
      <div className="ms-lufs">{lufsVal > -70 ? lufsVal.toFixed(1) : "-"}</div>
      <div className="ms-readout">{Math.round(volume * 100)}%</div>
      {!isMaster && (
        <input
          type="range"
          className="ms-pan-fader"
          min={-1}
          max={1}
          step={0.01}
          value={pan}
          onChange={(e) => setPan(parseFloat(e.target.value))}
          onMouseDown={() => useAutomationStore.getState().setLastClickedParamID(2)}
          onMouseUp={commitPan}
          onBlur={commitPan}
          title={`Pan ${formatPan(pan)}`}
        />
      )}
      {!isMaster && (
        <div className="ms-buttons">
          <button
            className={`ms-btn ms-mute${track.effectiveMuted ? " active" : ""}${track.muted !== track.effectiveMuted ? " ms-btn--cascaded" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackMuted", { trackIndex: track.index, muted: !track.muted }).catch(console.error);
            }}
            title={track.muted !== track.effectiveMuted ? "Muted by parent folder" : "Mute"}
          >M</button>
          <button
            className={`ms-btn ms-solo${track.effectiveSoloed ? " active" : ""}${track.soloed !== track.effectiveSoloed ? " ms-btn--cascaded" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackSoloed", { trackIndex: track.index, soloed: !track.soloed }).catch(console.error);
            }}
            title={track.soloed !== track.effectiveSoloed ? "Soloed by parent folder" : "Solo"}
          >S</button>
          <button
            className={`ms-btn ms-arm${track.armed ? " active" : ""}`}
            onClick={(e) => {
              e.stopPropagation();
              rpc.call("project.setTrackArmed", { trackIndex: track.index, armed: !track.armed }).catch(console.error);
            }}
            title="Arm"
          >R</button>
        </div>
      )}
      {!isMaster && sends.length > 0 && (
        <div className="ms-sends">
          {sends.map((s) => (
            <div
              key={s.sendIndex}
              className={`ms-send-row${s.bypassed ? " ms-send-bypassed" : ""}`}
              style={{ "--send-color": s.isPreFader ? "var(--send-pre-color)" : "var(--send-post-color)" } as React.CSSProperties}
            >
              <div className="ms-send-label">S{s.sendIndex + 1}</div>
              <input
                type="range"
                className="ms-send-fader"
                min={0}
                max={2}
                step={0.01}
                value={s.level}
                onChange={(e) => setSendLevel(s.sendIndex, parseFloat(e.target.value))}
                title={`Send ${s.sendIndex + 1}: ${s.isPreFader ? "Pre" : "Post"}`}
              />
              <button
                className="ms-send-mode-btn"
                onClick={() => toggleSendMode(s.sendIndex)}
                title={s.isPreFader ? "Pre-fader (click for Post)" : "Post-fader (click for Pre)"}
              >
                {s.isPreFader ? "P" : "O"}
              </button>
              <button
                className={`ms-send-bypass-btn${s.bypassed ? " active" : ""}`}
                onClick={() => toggleSendBypass(s.sendIndex)}
                title={s.bypassed ? "Unbypass" : "Bypass"}
              >
                B
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function formatPan(pan: number): string {
  if (pan === 0) return "C";
  const pct = Math.round(Math.abs(pan) * 100);
  return pan < 0 ? `L${pct}` : `R${pct}`;
}

function MeterBar({ value, rmsValue }: { value: number; rmsValue: number }) {
  let cls = "ms-fill";
  if (value > 90) cls += " ms-clip";
  else if (value > 75) cls += " ms-hot";
  return (
    <div className="ms-meter">
      <div className="ms-rms-fill" style={{ height: `${rmsValue}%` }} />
      <div className={cls} style={{ height: `${value}%` }} />
    </div>
  );
}