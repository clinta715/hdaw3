import { useMemo } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import { getVisibleTracks } from "../utils/timelineUtils";
import "./SessionView.css";

const SCENE_COUNT = 8;

export default function SessionView() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];
  const launchedScene = snapshot?.launchedScene ?? -1;

  const visibleTracks = useMemo(() => getVisibleTracks(tracks), [tracks]);

  const clipsBySlot = useMemo(() => {
    const map = new Map<string, (typeof clips)[0]>();
    for (const clip of clips) {
      if (clip.sceneIndex != null && clip.sceneIndex >= 0) {
        map.set(`${clip.trackIndex}-${clip.sceneIndex}`, clip);
      }
    }
    return map;
  }, [clips]);

  const handleSceneLaunch = (sceneIndex: number) => {
    rpc.call("session.launchScene", { sceneIndex }).catch(console.error);
  };

  const handleSlotClick = (trackIndex: number, sceneIndex: number) => {
    const key = `${trackIndex}-${sceneIndex}`;
    const existing = clipsBySlot.get(key);
    if (existing) {
      useUiStore.getState().selectClip(existing.clipId, trackIndex);
    } else {
      rpc
        .call("session.createClip", { trackIndex, sceneIndex, isMidi: true })
        .then(() => useProjectStore.getState().syncSnapshot(rpc))
        .catch(console.error);
    }
  };

  return (
    <div className="sv-root">
      <div className="sv-scene-buttons">
        {Array.from({ length: SCENE_COUNT }, (_, i) => (
          <button
            key={i}
            className={`sv-scene-btn${launchedScene === i ? " sv-scene-btn--active" : ""}`}
            onClick={() => handleSceneLaunch(i)}
            title={`Launch Scene ${i + 1}`}
          >
            Scene {i + 1}
          </button>
        ))}
        <button
          className="sv-scene-btn sv-stop-all-btn"
          onClick={() => rpc.call("session.stopAll", {}).catch(console.error)}
          title="Stop all playing session clips"
        >
          Stop All
        </button>
      </div>
      <div className="sv-grid">
        {visibleTracks.map((track) => (
          <div key={track.index} className="sv-track-col">
            <div
              className="sv-track-header"
              style={{ borderBottom: `3px solid ${colorStr(track.color)}` }}
            >
              {track.name}
            </div>
            {Array.from({ length: SCENE_COUNT }, (_, si) => {
              const key = `${track.index}-${si}`;
              const clip = clipsBySlot.get(key);
              return (
                <div
                  key={si}
                  className={`sv-slot${clip ? " sv-slot--filled" : ""}${clip && launchedScene === si ? " sv-slot--playing" : ""}`}
                  style={clip ? { background: colorStr(track.color) } : undefined}
                  onClick={() => handleSlotClick(track.index, si)}
                  title={clip ? clip.name : "Click to create clip"}
                >
                  {clip && <span className="sv-slot-name">{clip.name}</span>}
                </div>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  );
}
