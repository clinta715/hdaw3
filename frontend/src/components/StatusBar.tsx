import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import "./StatusBar.css";

export default function StatusBar() {
  const transport = useTransportStore((s) => s.transport);
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);

  const selectedTrack = selectedTrackIndex != null ? snapshot?.tracks[selectedTrackIndex] : null;

  return (
    <div className="status-bar">
      <span className="sb-field">♩ {transport.bpm.toFixed(1)}</span>
      <span className="sb-field">{transport.sampleRate} Hz</span>
      {selectedTrack && (
        <span className="sb-field">Track: {selectedTrack.name}</span>
      )}
      <span className="sb-field">{selectedClipIds.size} selected</span>
      {transport.isRecording && <span className="sb-field sb-rec">● REC</span>}
    </div>
  );
}
