# Recording — Phase Design

## Overview
Add audio recording to HDAW. Users can arm tracks, press record, capture live audio input to disk, and have the recorded clip appear on the timeline.

## Execution Order
1. AudioRecorder class (disk writing via ThreadedWriter)
2. Transport record state (isRecording, record arm ID)
3. Track record-arm UI (wire R button)
4. Recording integration in MainAudioProcessor::processBlock
5. Transport controls (Record button, keyboard shortcuts)

---

## 1. AudioRecorder

### New class: `HDAW::AudioRecorder`
- `src/engine/AudioRecorder.h` / `.cpp`
- Owns `juce::AudioFormatWriter::ThreadedWriter` for async disk writing
- Internal SPSC FIFO (`juce::AbstractFifo` + buffer) for audio thread → writer thread

### Methods
- `startRecording(const File& path, double sampleRate, int numChannels)` — create WAV writer, start thread
- `stopRecording()` — flush and close writer, return recorded file path
- `isRecording() const`
- `processBlock(const AudioBuffer<float>& buffer)` — copy input samples to FIFO
- Background thread pops from FIFO and writes to disk via ThreadedWriter

### State
- `std::unique_ptr<AudioFormatWriter::ThreadedWriter> writer`
- `juce::AbstractFifo fifo` (capacity ~65536 samples)
- `std::vector<float> fifoBuffer`
- `juce::File outputFile`
- `std::atomic<bool> active{false}`

---

## 2. Transport & Model Changes

### TransportManager
- `std::atomic<bool> isRecording{false}`
- `void setRecording(bool)`, `bool isRecordingNow()`
- Record position advances same as play position via `advance()`

### ProjectModel IDs
- `DECLARE_ID(isArm)` — per-track record arm property

---

## 3. Track Record-Arm

### TrackHeaderWidget
- The "R" button on track headers already exists in the UI but clicking it does nothing (no handler for type==3 in hitTest)
- Wire click handler: toggle `IDs::isArm` on the track tree
- Visual: R button lights up red when armed (existing drawToggle supports `onColor`)

---

## 4. MainAudioProcessor Integration

### New members
- `std::unique_ptr<HDAW::AudioRecorder> audioRecorder`
- Recording file path management

### processBlock changes
- Before graph processing, capture input buffer into AudioRecorder if recording
- After graph processing, transport advances as before

### Recording flow
- **Record pressed:** create temp file path, call `audioRecorder->startRecording()`, set `transportManager.setRecording(true)`
- **Stop pressed:** call `audioRecorder->stopRecording()`, set `transportManager.setRecording(false)`, create clip ValueTree on armed track at record start position, call `rebuildRoutingGraph()`

### File naming
- Recordings stored in `~/.hdaw/recordings/rec_YYYYMMDD_HHMMSS.wav`
- After stop, clip references this file

---

## 5. Transport UI

### TimelineToolbar additions
- Record button (red circle, toggle) alongside existing snap/zoom controls
- Connects to a signal that MainWindow handles to start/stop recording

### Keyboard shortcuts
- R — toggle recording (if playing, stays playing; if stopped, starts play+record)
- Space — play/stop (stop also stops recording)

---

## 6. Files Changed

### New
- `src/engine/AudioRecorder.h`
- `src/engine/AudioRecorder.cpp`

### Changed
- `src/engine/TransportManager.h` — add isRecording, setRecording
- `src/engine/MainAudioProcessor.h` — add AudioRecorder, start/stop recording methods
- `src/engine/MainAudioProcessor.cpp` — capture input in processBlock
- `src/engine/AudioEngine.h` — expose recorder methods
- `src/engine/AudioEngine.cpp` — wire recording commands
- `src/model/ProjectModel.h` — add isArm ID
- `src/ui/TrackHeaderWidget.cpp` — wire R button
- `src/ui/TimelineToolbar.h` — add record button signal
- `src/ui/TimelineToolbar.cpp` — add record button
- `src/ui/MainWindow.cpp` — wire recording actions
- `CMakeLists.txt` — add AudioRecorder.cpp

---

## 7. Testing

1. Arm a track, press Record, play audio into input, press Stop — verify clip appears on timeline
2. Playback the recorded clip — verify audio is correct
3. Record on different armed tracks — verify each track gets its own clip
4. Record without arming — verify no clip created
5. Space stops recording as well as playback
