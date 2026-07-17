export interface TransportSnapshot {
  bpm: number;
  isPlaying: boolean;
  isLooping: boolean;
  isRecording: boolean;
  loopStart: number;
  loopEnd: number;
  currentTimeSeconds: number;
  sampleRate: number;
}

export interface TrackSnapshot {
  index: number;
  name: string;
  color: number;
  volume: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  armed: boolean;
  inputMonitor: boolean;
  height: number;
  midiChannel: number;
  clipCount: number;
}

export interface GainEnvelopePoint {
  time: number;
  gain: number;
}

export interface ClipSnapshot {
  clipId: number;
  trackIndex: number;
  name: string;
  sourceFile: string;
  startBeat: number;
  durationBeats: number;
  offset: number;
  gain: number;
  fadeIn: number;
  fadeOut: number;
  looping: boolean;
  isMidi: boolean;
  sourceBpm: number;
  stretchMode: number;
  stretchRatio: number;
  sourceDuration: number;
  gainEnvelope: GainEnvelopePoint[];
}

export interface NoteSnapshot {
  noteId: number;
  pitch: number;
  velocity: number;
  startBeat: number;
  durationBeats: number;
}

export interface ProjectSnapshot {
  name: string;
  transport: TransportSnapshot;
  tracks: TrackSnapshot[];
  clips: ClipSnapshot[];
  scaleRoot: number;
  scaleMode: number;
}

export interface MeterLevels {
  l: number;
  r: number;
}

export interface MetersPayload {
  master: MeterLevels;
  tracks: MeterLevels[];
}
