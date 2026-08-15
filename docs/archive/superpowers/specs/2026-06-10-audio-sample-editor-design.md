# Audio Sample Editor — Design Spec

**Date:** 2026-06-10
**Status:** Approved design (pre-implementation)
**Estimate:** ~250–350 lines of new code

## 1. Motivation

Currently, double-clicking an audio clip in the timeline routes the
user to the global Mixer panel — there is no per-clip audio editor.
This spec defines a dedicated bottom-stack editor for audio clips
with waveform display, gain control, fade curve editing, source-file
management, and transport sync.

## 2. New Files

| File | Purpose | ~Lines |
|------|---------|--------|
| `src/ui/AudioClipEditorWidget.h` / `.cpp` | Main container widget; owns waveform + control bar | 100 |
| `src/ui/AudioWaveformWidget.h` / `.cpp` | Waveform rendering, zoom/scroll, interaction | 150 |

## 3. Modified Files

| File | Change |
|------|--------|
| `src/ui/MainWindow.cpp` | Add tab 4 to bottomStack, wire `clipSelected` for `"audio"` |
| `CMakeLists.txt` | Add `AudioClipEditorWidget` and `AudioWaveformWidget` sources |

## 4. AudioClipEditorWidget (Container)

**Analogous to:** `PianoRollWidget`

### Header bar
- Clip name label (left)
- Close button (right) — emits `clipClosed()` signal
- Zoom +/- buttons (right, next to close)

### Control bar (horizontal strip, ~30% of height, below waveform)
- **Gain**: horizontal `QSlider` (range -36 dB to +12 dB, default 0 dB, displayed as dB float), dB label
- **Fade In**: `QDoubleSpinBox` (seconds, range 0 to duration/2), linked to waveform fade handle
- **Fade Out**: `QDoubleSpinBox` (seconds, range 0 to duration/2), linked to waveform fade handle
- **Loop**: `QCheckBox` ("∞")
- **Offset**: `QDoubleSpinBox` (seconds), modifies `IDs::offset`
- **Duration**: `QDoubleSpinBox` (seconds), modifies `IDs::duration`
- **Source info**: `QLabel` with file path (truncated + tooltip), sample rate / bit depth / length from `AudioThumbnail`

### API
```cpp
class AudioClipEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioClipEditorWidget(HDAW::ProjectPool& pool,
                                   juce::AudioFormatManager& fmtMgr,
                                   QWidget* parent = nullptr);
    void loadClip(juce::ValueTree clipTree);
    void clear();
    void reloadFromAudioEngine();  // refresh thumbnail + metadata
    void updatePlayhead(double seconds);  // called by MainWindow timer

signals:
    void clipClosed();

private:
    juce::ValueTree currentClip;
    AudioWaveformWidget* waveform;
    // control widgets...
};
```

## 5. AudioWaveformWidget (Display + Interaction)

**Analogous to:** `NoteGridWidget`

### Data sources
- `juce::AudioThumbnail` — loaded from `sourceFile` via `ProjectPool`
- Clip `ValueTree` — reads/writes `offset`, `duration`, `fadeIn`, `fadeOut`, `gain`, `looping`

### Rendering
- Waveform drawn via `AudioThumbnail::drawChannels()` into `juce::Image`, converted to `QImage`
- Viewport maps `[offset, offset + duration]` to widget width
- **Horizontal zoom**: pixels-per-second (same pattern as `NoteGridWidget::pixelsPerBeat`)
- **Horizontal scroll**: `QScrollBar` or wheel; pans through the clip
- **Vertical zoom**: Ctrl+wheel to scale waveform amplitude (future: currently auto-fit)
- **Playhead**: vertical line at transport position, updated by timer (50ms). Not drawn if outside clip range.
- **Fade overlays**: shaded triangles at clip start/end, matching `ClipItem::paint` style, linked to `fadeIn` / `fadeOut`
- **Region selection**: semi-transparent highlight rect over selected range

### Mouse interaction
| Action | Behavior |
|--------|----------|
| Click on fade-in region | Drag to adjust `fadeIn` duration |
| Click on fade-out region | Drag to adjust `fadeOut` duration |
| Click+drag on waveform body | Region selection |
| Right-click | Context menu (future: split, rename) |
| Mouse wheel | Horizontal scroll |
| Ctrl+wheel | Horizontal zoom |

### API
```cpp
class AudioWaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioWaveformWidget(HDAW::ProjectPool& pool,
                                 juce::AudioFormatManager& fmtMgr,
                                 QWidget* parent = nullptr);
    void setClip(juce::ValueTree clip);
    void reloadThumbnail();
    void setPlayheadPosition(double seconds);
    void setPixelsPerSecond(double pps);
    void zoomIn();
    void zoomOut();

signals:
    void gainChanged(double gain);
    void fadeInChanged(double seconds);
    void fadeOutChanged(double seconds);
    void offsetChanged(double seconds);
    void durationChanged(double seconds);
    void regionSelected(double startBeat, double endBeat);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
};
```

## 6. Integration (MainWindow)

### Bottom stack tab
- Add `AudioClipEditorWidget* audioEditor` as tab 4 (index 4) in `bottomStack`
- Add "Audio" tab button in the tab `QButtonGroup`

### clipSelected routing
```cpp
// In the existing clipSelected lambda:
if (type == "midi") {
    pianoRollWidget->loadClip(clipTree);
    bottomStack->setCurrentIndex(1);
} else if (type == "audio") {
    audioEditor->loadClip(clipTree);
    bottomStack->setCurrentIndex(4);
} else {
    bottomStack->setCurrentIndex(0);
}
```

### Playhead sync
- MainWindow's existing transport timer also calls `audioEditor->updatePlayhead(transportSeconds)`
- Already done if the timer iterates all editors; otherwise add one call

### Tab sync
- The existing `currentChanged` bridge in `MainWindow::setupLayout` already syncs button checked-states
- No extra wiring needed

## 7. Data Model

All edits go directly to the clip's `ValueTree` properties with the
existing undo manager from `ProjectModel`:

| Property | Type | Control |
|----------|------|---------|
| `IDs::gain` | double (float) | Gain slider |
| `IDs::fadeIn` | double (seconds) | Fade In spinbox / waveform drag |
| `IDs::fadeOut` | double (seconds) | Fade Out spinbox / waveform drag |
| `IDs::looping` | bool | Loop checkbox |
| `IDs::offset` | double (seconds) | Offset spinbox |
| `IDs::duration` | double (seconds) | Duration spinbox |
| `IDs::sourceFile` | string | Read-only display in control bar |

## 8. Out of Scope (v1)
- Audio clip splitting / slicing
- Time-stretch / pitch-shift
- Reverse playback
- Zero-crossing snapping
- Multiple clip editing (only one at a time)
- Clip rename in-editor
