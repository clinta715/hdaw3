# HDAW — Comprehensive C++/JUCE/Qt Conversion Plan

## 1. Vision & Strategy

Convert the existing Rust-based **HDAW** (Holofonic Digital Audio Workstation) to a modern C++20 application. This transition moves from `egui` to **Qt 6** for a richer desktop experience and from `cpal`/`clack-host` to **JUCE 8** for industry-standard audio stability and plugin compatibility.

### Key Technology Stack
*   **Audio Engine:** [JUCE 8](https://juce.com/) (Audio I/O, MIDI, Plugin Hosting, DSP primitives).
*   **UI Framework:** [Qt 6 Widgets](https://www.qt.io/) (Main Window, Tiled Layout, Panels, Widgets).
*   **Build System:** CMake 3.24+.
*   **Data Model:** `juce::ValueTree` (Unified Project State, Undo/Redo, and Serialization).

---

## 2. Exhaustive Feature Mapping (Rust → C++)

### 2.1 Audio Engine & DSP
| Rust Feature | C++ / JUCE Implementation |
| :--- | :--- |
| **Audio I/O (cpal)** | `juce::AudioDeviceManager` + `juce::AudioSourcePlayer`. |
| **Multi-track Summing** | Custom `juce::AudioProcessorGraph` or manual summing in `processBlock`. |
| **Clip Fades & Crossfades** | Port `compute_fade_gain()` logic using `juce::LinearSmoothedValue`. |
| **Metronome** | Sine oscillator in `processBlock`, triggered by `juce::AudioPlayHead` position. |
| **Audio Recording** | `juce::AudioFormatWriter::ThreadedWriter` with SPSC FIFO capture. |
| **Audio Mixdown** | `juce::AudioProcessor::processBlock` in non-real-time mode to `WavWriter`. |
| **Tempo Track** | Port `TempoEvent`/`TimeSigEvent` logic to a custom `HDAW::TempoMap` class. |
| **SPSC Param Pipeline** | Port `ParamRingBuffer` using `juce::AbstractFifo` + `std::atomic`. |
| **Internal FX** | Re-implement Gain, EQ, Compressor, Reverb, Delay using `juce_dsp` module. |
| **CLAP Hosting** | Use JUCE 8's native CLAP hosting (or VST3/AU wrappers). |

### 2.2 Timeline & Arrangement
| Rust Feature | C++ / Qt Implementation |
| :--- | :--- |
| **Tiled Layout** | `QMainWindow` with `QSplitter` (Central, Right, Bottom panels). |
| **Waveform Rendering** | `juce::AudioThumbnailCache` + custom Qt Widget using `QPainter`. |
| **Clip Interaction** | Drag/Trim/Snap implemented via `QGraphicsView` or custom `QWidget` events. |
| **Track Headers** | Custom Qt Widgets with `QPushButton` (M/S/R), `QSlider` (Vol/Pan), and VU meters. |
| **Automation Lanes** | Bezier-curve rendering in Qt; playback via `juce::ValueTree` listeners in engine. |
| **Time Ruler** | Multi-resolution ruler (Bars:Beats/Time) using `HDAW::TempoMap`. |
| **Loop Region** | Visual markers on Ruler; engine-side wrapping in `processBlock`. |

### 2.3 MIDI & Sequencing
| Rust Feature | C++ / JUCE Implementation |
| :--- | :--- |
| **Piano Roll** | Custom Qt Editor; Piano keys (Left), Note Grid (Center), Velocity (Bottom). |
| **MIDI Dispatch** | `juce::MidiMessageSequence` mapped to plugin MIDI input buffers. |
| **MIDI Import** | `juce::MidiFile` parser; populates `juce::ValueTree`. |
| **Controller Lanes** | Tabbed lanes in Piano Roll for CC data rendering/editing. |

### 2.4 Project & Infrastructure
| Rust Feature | C++ / JUCE Implementation |
| :--- | :--- |
| **Dual-Model Sync** | **REPLACED** by `juce::ValueTree`. UI and Engine both observe the same tree. |
| **Undo/Redo System** | `juce::UndoManager` attached to the `ValueTree`. |
| **Serialization (RON)** | `juce::ValueTree::toXmlString()` or `toJsonString()`. |
| **Preferences** | `juce::PropertiesFile` or Qt's `QSettings`. |
| **Audio Pool** | `HDAW::ProjectPool` managing `juce::AudioFormatReader` instances. |

---

## 3. Detailed Development Phases

### Phase 1: Foundation (Weeks 1-4) [DONE]
*   **1.1 Build System:** [DONE] CMake setup to fetch JUCE via `FetchContent` and find Qt6.
*   **1.2 Engine Core:** [DONE] Initialize `juce::AudioDeviceManager`. Setup `MainAudioProcessor`.
*   **1.3 Main Window:** [DONE] Qt `QMainWindow` with the 3-panel `QSplitter` layout.
*   **1.4 SPSC Bridge:** [DONE] Implement the lock-free queue for UI → Engine param updates.
*   **1.5 Project Model:** [DONE] Define the `ValueTree` schema (Tracks, Clips, Transport, Loop).

### Phase 2: Audio Engine & Mixer (Weeks 5-8)
*   **2.1 Track Processor:** [DONE] Implement `HDAW::Track` class (derived from `juce::AudioProcessor`).
*   **2.2 Summing & Routing:** Multi-pass mixing for Groups and Send FX (from Rust Phase 5).
*   **2.3 Transport Logic:** [DONE] Play/Stop/Loop/Seek logic with sample-accurate positioning.
*   **2.4 Metering:** [DONE] Port the VU meter logic using `juce::AudioVisualiserComponent` concepts for Qt.
*   **2.5 Internal FX Parity:** Port EQ, Compressor, Reverb, Delay using `juce_dsp`.

### Phase 3: Timeline & Waveforms (Weeks 9-12)
*   **3.1 Waveform Cache:** [DONE] Setup `juce::AudioThumbnailCache` (parities with Rust `waveform_cache`).
*   **3.2 Timeline Widget:** Implement scrolling/zooming arrangement view in Qt.
*   **3.3 Clip Rendering:** Port rounded-rect clips with waveform overlays and fade handles.
*   **3.4 Interaction Engine:** Port Snap-to-grid, Clip Trimming, and Move commands.
*   **3.5 Fade System:** Implement the `LinearSmoothedValue` based gains for clip edges.

### Phase 4: MIDI & Piano Roll (Weeks 13-16)
*   **4.1 MIDI Model:** Store MIDI notes as children of Clip nodes in the `ValueTree`.
*   **4.2 Piano Roll UI:** Grid rendering, note drawing, and velocity lane (from Rust v0.8.0).
*   **4.3 MIDI Dispatcher:** High-priority MIDI event delivery to hosted plugins.
*   **4.4 MIDI Import:** Port the `.mid` file loader into the Project Pool.

### Phase 5: Automation & Plugins (Weeks 17-20)
*   **5.1 Automation Logic:** Port Bezier interpolation and SPSC delivery of automation points.
*   **5.2 Automation UI:** Toggled lanes under track headers with "Add Point" interaction.
*   **5.3 Plugin Hosting:** Implement the `AudioPluginFormatManager` for VST3/CLAP.
*   **5.4 FX Chain UI:** Tabbed "FX Chain" mode in the bottom panel for plugin management.

### Phase 6: Recording & Export (Weeks 21-24)
*   **6.1 Disk Recording:** Implement `ThreadedWriter` for incremental WAV capture (Rust Phase 2).
*   **6.2 Offline Render:** Implement the "Export Audio" dialog and background render task.
*   **6.3 Project Pool UI:** Tabbed "Browser" mode in the right panel for asset management.
*   **6.4 Final Polish:** Custom QSS (Qt Style Sheets) for the HDAW dark theme (Parity with `GUI_LAYOUT_DETAILS.md`).

---

## 4. Technical Constraints & Safety

### 4.1 Real-Time Safety (The "Sacred" Rule)
The audio thread (JUCE `processBlock`) must **NEVER**:
*   Allocate memory (`new`, `malloc`, `std::vector::push_back`).
*   Acquire blocking locks (`std::mutex::lock`).
*   Perform File I/O.
*   Call UI functions.

### 4.2 ValueTree Synchronicity
*   **UI → Engine:** UI modifies `ValueTree`. `ValueTree::Listener` on the Message Thread sends SPSC messages to the Engine.
*   **Engine → UI:** Engine writes to atomics (e.g., VU meters). UI polls atomics at 60Hz.

---

## 5. Risk Assessment & Mitigation

| Risk | Mitigation |
| :--- | :--- |
| **UI Performance** | Use `Qt Quick` or `QGraphicsView` if standard `QWidget` hierarchy becomes too slow for 1000+ clips. |
| **Undo Bloat** | JUCE `UndoManager` handles this gracefully, but we must ensure we don't store large binary buffers in the `ValueTree`. |
| **Sample Rate Mismatch** | Leverage `juce::ResamplingAudioSource` during import (parity with Rust `WAV import with SRC`). |
| **Plugin Stability** | Use JUCE's standard hosting wrappers which provide robust error handling for crashing plugins. |
