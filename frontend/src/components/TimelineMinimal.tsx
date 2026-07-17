import { useMemo } from "react";
import { useProjectStore } from "../store/projectStore";
import "./TimelineMinimal.css";

const PIXELS_PER_BEAT = 40;
const TRACK_HEIGHT = 56;

export default function TimelineMinimal() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];

  const clipsByTrack = useMemo(() => {
    const map = new Map<number, typeof clips>();
    for (const c of clips) {
      const group = map.get(c.trackIndex) ?? [];
      group.push(c);
      map.set(c.trackIndex, group);
    }
    return map;
  }, [clips]);

  const maxEnd = clips.reduce((max, c) => Math.max(max, c.startBeat + c.durationBeats), 4);
  const totalW = Math.max(maxEnd * PIXELS_PER_BEAT, 400);

  return (
    <div className="timeline-minimal">
      {tracks.length === 0 && (
        <div className="tl-empty">No tracks</div>
      )}
      <div className="tl-tracks" style={{ height: tracks.length * TRACK_HEIGHT }}>
        {tracks.map((track, idx) => {
          const trackClips = clipsByTrack.get(track.index) ?? [];
          return (
            <div
              key={track.index}
              className="tl-track-row"
              style={{
                top: idx * TRACK_HEIGHT,
                height: TRACK_HEIGHT,
              }}
            >
              {trackClips.map((clip) => (
                <div
                  key={clip.clipId}
                  className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}`}
                  style={{
                    left: clip.startBeat * PIXELS_PER_BEAT,
                    width: Math.max(4, clip.durationBeats * PIXELS_PER_BEAT),
                    height: TRACK_HEIGHT - 8,
                    top: 4,
                  }}
                >
                  <span className="tl-clip-name">{clip.name ?? `Clip ${clip.clipId}`}</span>
                </div>
              ))}
            </div>
          );
        })}
      </div>
    </div>
  );
}
