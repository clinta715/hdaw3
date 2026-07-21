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
  // Note: meter levels are NOT on the snapshot. The backend's
  // toJson(TrackSnapshot) emits only the fields below; per-track meter
  // data arrives via the separate `notify.meters` push (see meterStore).
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

export interface AutomationLaneSnapshot {
  laneIndex: number;
  name: string;
  paramID: number;
  enabled: boolean;
}

export interface AutomationPointSnapshot {
  time: number;
  value: number;
}

export interface FxSlotSnapshot {
  slotIndex: number;
  fxType: string;
  pluginId: string;
  pluginName: string;
  bypassed: boolean;
  paramCount: number;
}

export interface AutomatableParamSnapshot {
  slotIndex: number;
  paramIndex: number;
  name: string;
  automatable: boolean;
}

export interface MetersPayload {
  master: MeterLevels;
  tracks: MeterLevels[];
}

export interface WaveformPeaks {
  peaks: number[];  // interleaved min/max pairs
  sampleRate: number;
  numSamples: number;
}

export interface ScaleModeInfo {
  index: number;
  name: string;
  intervals: number[];
}

export interface ChordTypeInfo {
  index: number;
  name: string;
  intervals: number[];
}

export interface ProgressionPatternInfo {
  index: number;
  name: string;
  chords: { degree: number; chordType: number }[];
}

export interface StyleInfo {
  index: number;
  name: string;
}

export interface GenerateResult {
  clipId: number;
  noteCount: number;
}
