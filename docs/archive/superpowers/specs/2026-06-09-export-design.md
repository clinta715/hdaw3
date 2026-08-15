# Export/Mixdown — Phase Design

## Overview
Add offline mixdown/export to HDAW. Users can render the entire project to a WAV file via File → Export Audio, with a progress dialog.

## Execution Order
1. ExportManager class (separate render graph, background render loop)
2. Export dialog UI (QDialog with file picker, progress bar)
3. Menu bar integration (File → Export Audio)

---

## 1. ExportManager

### New class: `HDAW::ExportManager`
- `src/engine/ExportManager.h` / `.cpp`

### Members
- `juce::AudioProcessorGraph renderGraph`
- `std::unique_ptr<HDAW::RoutingManager> routingManager`
- Own transport state (`double sampleRate`, `int64_t currentSample`, `double bpm`)
- `std::atomic<bool> cancelFlag{false}`
- `std::function<void(float)> progressCallback`

### Methods
- `bool startExport(ProjectModel& model, AudioFormatManager& formatManager, PluginManager* pm, const File& outputPath, double startTime, double duration)` — build render graph, start render thread
- `void cancel()` — set cancel flag
- `bool isExporting() const`

### Render loop (background thread)
```
1. Build AudioProcessorGraph + RoutingManager from ValueTree
2. Create InternalPlayHead wired to export transport state
3. graph.setPlayHead(playhead)
4. graph.prepareToPlay(projectSampleRate, blockSize)
5. Create WavAudioFormat writer to outputPath
6. Set transport position = startTime
7. totalBlocks = (duration * sampleRate) / blockSize
8. For each block:
   - If cancelFlag, break
   - Call graph.processBlock(buffer, midi)
   - writer->write(buffer)
   - Advance transport by blockSize
   - Update progress: blockIndex / totalBlocks
9. Close writer
10. Report completion
```

---

## 2. Export Dialog

### New file: `src/ui/ExportDialog.h` / `.cpp` (or inline in MainWindow)

Simple `QDialog`:
- File: `QLineEdit` + browse button → `QFileDialog::getSaveFileName()` with `.wav` filter
- Range: `QComboBox` — "Full Project", "Loop Range"
- Progress: `QProgressBar` (0-100)
- Cancel/Close button
- Status label

On start:
- Validate parameters
- Call `engine.getMainProcessor()->startExport(...)` or through ExportManager
- Start a `QTimer` to poll progress (or use signal)

On complete:
- Show "Export complete" message
- Change Cancel to Close
- Option to "Open containing folder"

---

## 3. Integration

### MainWindow
- File → Export Audio... menu item (Ctrl+E)
- Opens ExportDialog
- Disabled while already exporting

### Files
- **New:** `src/engine/ExportManager.h`, `src/engine/ExportManager.cpp`, `src/ui/ExportDialog.h`, `src/ui/ExportDialog.cpp`
- **Changed:** `src/ui/MainWindow.h/.cpp`, `CMakeLists.txt`
