import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import "./TrackHeaders.css";

const TRACK_TYPE_ICONS: Record<number, string> = {
  0: "\u25B2",       // audio: triangle
  1: "\u266B",       // instrument: music note
  2: "\u25BC",       // folder: down triangle
};

const TRACK_TYPE_LABELS: Record<number, string> = {
  0: "AUDIO",
  1: "MIDI",
  2: "FOLDER",
};

const TRACK_TYPE_CLASSES: Record<number, string> = {
  0: "audio",
  1: "instrument",
  2: "folder",
};

const TRACK_TYPE_COLORS: Record<number, string> = {
  0: "#4a9eff",      // audio: blue
  1: "#9b59b6",      // instrument: purple
  2: "#f39c12",      // folder: orange
};

export default function TrackHeaders() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];

  // Build set of hidden track indices (children of collapsed folders)
  const hiddenIndices = new Set<number>();
  for (const track of tracks) {
    if (track.trackType === 2 && track.isCollapsed) {
      for (const child of tracks) {
        if (child.parentId === track.index) {
          hiddenIndices.add(child.index);
        }
      }
    }
  }
  const visibleTracks = tracks.filter(t => !hiddenIndices.has(t.index));

  const trackMeters = useMeterStore((s) => s.tracks);
  const selectClip = useUiStore((s) => s.selectClip);

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

  const handleMonitor = (idx: number, monitor: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackInputMonitor", { trackIndex: idx, monitor: !monitor }).catch(console.error);
  };

  const handleColorChange = (idx: number, e: React.MouseEvent) => {
    e.stopPropagation();
    const input = document.createElement("input");
    input.type = "color";
    input.value = colorStr(tracks[idx].color);
    input.addEventListener("input", () => {
      const hex = input.value.replace("#", "");
      const color = parseInt(hex, 16);
      rpc.call("project.setTrackColor", { trackIndex: idx, color }).catch(console.error);
    });
    input.click();
  };

  const handleHeightDrag = (idx: number, startY: number, startH: number) => {
    const onMove = (me: MouseEvent) => {
      const delta = me.clientY - startY;
      const newH = Math.max(40, Math.min(200, startH + delta));
      rpc.call("project.setTrackHeight", { trackIndex: idx, height: newH });
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };

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
      {visibleTracks.map((track) => {
        const meter = trackMeters[track.index] ?? { l: 0, r: 0 };
        return (
        <div
          key={track.index}
          className={`th-row th-row--${TRACK_TYPE_CLASSES[track.trackType] ?? "audio"}`}
          style={{ paddingLeft: track.parentId != null && track.parentId >= 0 ? 20 : 0 }}
          onClick={() => selectClip(null, track.index)}
        >
          <div className="th-type-badge" style={{ color: TRACK_TYPE_COLORS[track.trackType] ?? TRACK_TYPE_COLORS[0] }}>
            {track.trackType === 2 && (
              <span
                className="th-chevron"
                onClick={(e) => {
                  e.stopPropagation();
                  rpc.call("project.setTrackCollapsed", { trackIndex: track.index, collapsed: !track.isCollapsed }).catch(console.error);
                }}
                title={track.isCollapsed ? "Expand folder" : "Collapse folder"}
              >
                {track.isCollapsed ? "\u25B6" : "\u25BC"}
              </span>
            )}
            {TRACK_TYPE_ICONS[track.trackType] ?? TRACK_TYPE_ICONS[0]}
          </div>
          <div
            className="th-color"
            style={{ background: colorStr(track.color), cursor: "pointer" }}
            onClick={(e) => handleColorChange(track.index, e)}
            title="Click to change track color"
          />
          <div className="th-info">
            <div className="th-name">{track.name}</div>
            <div className="th-type" style={{ color: TRACK_TYPE_COLORS[track.trackType] ?? TRACK_TYPE_COLORS[0] }}>
              {TRACK_TYPE_LABELS[track.trackType] ?? "AUDIO"}
            </div>
          </div>
          <div className="th-controls">
            <button
              className={`th-btn th-mute${track.muted || track.effectiveMuted ? " active" : ""}`}
              onClick={(e) => handleMute(track.index, track.muted, e)}
              title="Mute"
            >
              M
            </button>
            <button
              className={`th-btn th-solo${track.soloed || track.effectiveSoloed ? " active" : ""}`}
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
            <button
              className={`th-btn th-monitor${track.inputMonitor ? " active" : ""}`}
              onClick={(e) => handleMonitor(track.index, track.inputMonitor, e)}
              title="Input Monitor"
            >
              In
            </button>
          </div>
          <div className="th-values">
            <span className="th-vol">V:{Math.round(track.volume * 100)}%</span>
            <span className="th-pan">{formatPan(track.pan)}</span>
            <span
              className="th-midi-ch"
              title="MIDI Channel (click to edit)"
              onClick={(e) => {
                e.stopPropagation();
                const ch = prompt("MIDI Channel (1-16):", String(track.midiChannel + 1));
                if (ch) {
                  const num = parseInt(ch, 10);
                  if (num >= 1 && num <= 16) {
                    rpc.call("project.setTrackMidiChannel", { trackIndex: track.index, channel: num - 1 });
                  }
                }
              }}
            >
              Ch{track.midiChannel + 1}
            </span>
          </div>
          <div className="th-meters">
            <MeterBar value={meter.l} />
            <MeterBar value={meter.r} />
          </div>
          <div
            className="th-resize-handle"
            onMouseDown={(e) => {
              e.preventDefault();
              e.stopPropagation();
              handleHeightDrag(track.index, e.clientY, track.height);
            }}
          />
        </div>
        );
      })}
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
