import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import "./StatusBar.css";

export default function StatusBar() {
  const bpm = useTransportStore((s) => s.transport.bpm);
  const sampleRate = useTransportStore((s) => s.transport.sampleRate);
  const isRecording = useTransportStore((s) => s.transport.isRecording);
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const viewMode = useUiStore((s) => s.viewMode);
  const statusHint = useUiStore((s) => s.statusHint);

  const selectedTrack = selectedTrackIndex != null ? snapshot?.tracks[selectedTrackIndex] : null;

  let defaultHint: string;
  if (selectedClipIds.size === 1) {
    defaultHint = "1 clip selected — double-click to edit · Delete to remove";
  } else if (selectedClipIds.size > 1) {
    defaultHint = `${selectedClipIds.size} clips selected — drag to move · Alt+drag to duplicate`;
  } else if (selectedTrackIndex != null) {
    const trackName = snapshot?.tracks[selectedTrackIndex]?.name;
    defaultHint = trackName
      ? `${trackName} selected — M / S / Arm in the header`
      : `Track ${selectedTrackIndex + 1} selected — M / S / Arm in the header`;
  } else if (viewMode === "session") {
    defaultHint = "Session view — click a clip to launch · Tab toggles Arrange/Session";
  } else {
    defaultHint = "Arrange — drag in an empty lane to create · double-click to add a clip";
  }

  return (
    <div className="status-bar">
      <span className="sb-field">♩ {bpm.toFixed(1)}</span>
      <span className="sb-field">{sampleRate} Hz</span>
      {selectedTrack && (
        <span className="sb-field">Track: {selectedTrack.name}</span>
      )}
      <span className="sb-field">{selectedClipIds.size} selected</span>
      {isRecording && <span className="sb-field sb-rec">● REC</span>}
      <span className="sb-field sb-hint">{statusHint ?? defaultHint}</span>
    </div>
  );
}
