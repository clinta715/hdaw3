import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";
import MixerStrip from "./MixerStrip";
import "./Mixer.css";

export default function Mixer() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];
  const masterMeter = useMeterStore((s) => s.master);
  const trackMeters = useMeterStore((s) => s.tracks);

  return (
    <div className="mixer">
      {tracks.length === 0 && (
        <div className="mixer-empty">No tracks</div>
      )}
      <div className="mixer-strips">
        {tracks.map((track, i) => (
          <MixerStrip
            key={track.index}
            track={track}
            meter={trackMeters[i] ?? { l: 0, r: 0 }}
          />
        ))}
      </div>
      <div className="mixer-master">
        <MixerStrip
          track={{
            index: -1, name: "Master", color: 0x787880, type: "master",
            volume: 1, pan: 0, muted: false, soloed: false,
            meterL: 0, meterR: 0, armed: false, inputMonitor: false,
            height: 80, midiChannel: 0, clipCount: 0,
          }}
          meter={masterMeter}
          isMaster
        />
      </div>
    </div>
  );
}
