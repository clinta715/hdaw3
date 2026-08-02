export interface TransportSnapshot {
  bpm: number;
  isPlaying: boolean;
  isLooping: boolean;
  isRecording: boolean;
  punchEnabled?: boolean;
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
  trackType: number;      // 0=audio, 1=instrument, 2=folder
  childIds?: number[];    // folder tracks: indices of children
  parentId?: number;      // child tracks: index of parent folder (-1 = none)
  isCollapsed?: boolean;  // folder tracks: collapse state
  isHidden?: boolean;     // user-hidden track (still in project, not rendered)
  effectiveMuted: boolean;   // cascaded from parent folders
  effectiveSoloed: boolean;  // cascaded from parent folders
}

export interface GainEnvelopePoint {
  time: number;
  gain: number;
}

export interface SendSnapshot {
  sendIndex: number;
  level: number;
  isPreFader: boolean;
  bypassed: boolean;
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
  muted: boolean;
  isMidi: boolean;
  sourceBpm: number;
  stretchMode: number;
  stretchRatio: number;
  sourceDuration: number;
  isGhost: boolean;
  ghostSourceId: number;
  sceneIndex?: number;  // -1 or undefined = arrangement only, 0–7 = session scene
  gainEnvelope: GainEnvelopePoint[];
}

export interface NoteSnapshot {
  noteId: number;
  pitch: number;
  velocity: number;
  startBeat: number;
  durationBeats: number;
  chance: number;
  repeatCount: number;
  repeatRate: number;
  repeatCurve: number;
  occurrence: number;
  recurrence: number;
  noteGain: number;
  notePan: number;
  notePitch: number;
  noteTimbre: number;
  notePressure: number;
}

export interface ProjectSnapshot {
  name: string;
  transport: TransportSnapshot;
  tracks: TrackSnapshot[];
  clips: ClipSnapshot[];
  scaleRoot: number;
  scaleMode: number;
  launchedScene?: number;
  sceneCount?: number;
}

export interface TreeDelta {
  fullSync: boolean;
  clipsUpserted?: ClipSnapshot[];
  clipsRemoved?: number[];
  tracksUpserted?: TrackSnapshot[];
}

export interface MeterLevels {
  l: number;
  r: number;
  rmsL: number;
  rmsR: number;
  lufs: number;
}

export interface AutomationLaneSnapshot {
  laneIndex: number;
  name: string;
  paramID: number;
  enabled: boolean;
  mode?: string;
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

export interface MidiFxSlotSnapshot {
  slotIndex: number;
  fxType: string;
  bypassed: boolean;
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
