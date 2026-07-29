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

  const selectedTrack = selectedTrackIndex != null ? snapshot?.tracks[selectedTrackIndex] : null;

  return (
    <div className="status-bar">
      <span className="sb-field">♩ {bpm.toFixed(1)}</span>
      <span className="sb-field">{sampleRate} Hz</span>
      {selectedTrack && (
        <span className="sb-field">Track: {selectedTrack.name}</span>
      )}
      <span className="sb-field">{selectedClipIds.size} selected</span>
      {isRecording && <span className="sb-field sb-rec">● REC</span>}
    </div>
  );
}
