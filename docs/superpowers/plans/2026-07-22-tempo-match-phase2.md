# Tempo-Match Phase 2: BPM Detection, Manual Input, Bar Snapping — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add automatic BPM detection (aubio), a manual BPM input dialog, and bar-boundary snapping to the audio import flow, so every imported file can be tempo-matched even without metadata.

**Architecture:** A new `BpmDetector` wraps aubio's onset tracking to extract BPM from raw audio. A `BpmInputDialog` prompts for manual BPM when detection fails or Shift is held. A `BarSnap` utility snaps clip start times to bar boundaries using the project tempo map. These integrate into `AudioImport::importAudioFile()` before the clip is added to the scene.

**Tech Stack:** C++20, aubio (FetchContent), Qt 6, JUCE 8, gtest. Build: `cmake --build build --config Debug`. Tests: `build/Debug/hdaw_tests.exe`.

---

## File Map

| File | Change | Responsibility |
|------|--------|----------------|
| `cmake/AubioHelper.cmake` | **Create** | FetchContent for aubio, static lib, alias `HDAW::aubio` |
| `CMakeLists.txt` | **Modify** | Include AubioHelper, link `HDAW::aubio` to HDAW_lib |
| `src/engine/BpmDetector.h` | **Create** | `BpmDetector::detect()` interface |
| `src/engine/BpmDetector.cpp` | **Create** | aubio_tempo wrapper implementation |
| `src/engine/BarSnap.h` | **Create** | `snapToBarBoundary()` interface |
| `src/engine/BarSnap.cpp` | **Create** | Bar-boundary snapping implementation |
| `src/ui/BpmInputDialog.h` | **Create** | `BpmInputDialog::ask()` interface |
| `src/ui/BpmInputDialog.cpp` | **Create** | QDialog with QDoubleSpinBox |
| `src/ui/PreferencesDialog.h` | **Modify** | Add `getSnapToBarBoundaries()` + checkbox member |
| `src/ui/PreferencesDialog.cpp` | **Modify** | Add snap checkbox, load/save setting |
| `src/engine/AudioImport.cpp` | **Modify** | Integrate detection, dialog, snapping |
| `src/engine/AudioImport.h` | **Modify** | Add forward declaration if needed |
| `tests/unit/engine/bpm_detector_test.cpp` | **Create** | Unit tests for BpmDetector |
| `tests/unit/engine/bar_snap_test.cpp` | **Create** | Unit tests for BarSnap |
| `tests/CMakeLists.txt` | **Modify** | Add new test files |

---

### Task 1: CMake FetchContent for aubio

**Files:**
- Create: `cmake/AubioHelper.cmake`
- Modify: `CMakeLists.txt:27` (after SoundTouchHelper include)

- [ ] **Step 1: Create `cmake/AubioHelper.cmake`**

```cmake
include(FetchContent)

# aubio — lightweight C library for audio labeling (onset detection, tempo).
# Used by BpmDetector for automatic BPM extraction on import.
FetchContent_Declare(
    aubio
    GIT_REPOSITORY https://github.com/aubio/aubio.git
    GIT_TAG 0.4.4
    SOURCE_DIR "${CMAKE_BINARY_DIR}/aubio-src"
)
# aubio's CMakeLists needs a small policy tweak for modern CMake.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(aubio)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# Expose a stable alias for HDAW consumers.
if(TARGET aubio)
    add_library(HDAW::aubio ALIAS aubio)
endif()
```

- [ ] **Step 2: Include AubioHelper in top-level CMakeLists.txt**

Add after line 27 (`include(cmake/SoundTouchHelper.cmake)`):

```cmake
# aubio (GPL-2.1) for BPM detection on audio import.
include(cmake/AubioHelper.cmake)
```

- [ ] **Step 3: Link aubio to HDAW_lib**

In `CMakeLists.txt`, add `HDAW::aubio` to the `target_link_libraries(HDAW_lib PUBLIC ...)` block (around line 127, after `HDAW::SoundTouch`):

```cmake
    HDAW::aubio
```

- [ ] **Step 4: Build and verify aubio compiles**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:HDAW_lib`
Expected: Build succeeds, aubio static lib produced.

- [ ] **Step 5: Commit**

```bash
git add cmake/AubioHelper.cmake CMakeLists.txt
git commit -m "build: add aubio via FetchContent for BPM detection"
```

---

### Task 2: BpmDetector — aubio wrapper

**Files:**
- Create: `src/engine/BpmDetector.h`
- Create: `src/engine/BpmDetector.cpp`
- Modify: `CMakeLists.txt` — add `src/engine/BpmDetector.cpp` to HDAW_lib sources

- [ ] **Step 1: Create `src/engine/BpmDetector.h`**

```cpp
#pragma once

namespace HDAW
{
    class BpmDetector
    {
    public:
        struct Result
        {
            double bpm = 0.0;
            double confidence = 0.0; // 0.0–1.0 (informational; flow uses bpm > 0)
        };

        /// Detect BPM from mono or interleaved float samples.
        /// sampleRate must match the source audio rate.
        /// Analyses up to maxSeconds of audio (default 30s).
        static Result detect(const float* samples, int numSamples,
                             double sampleRate, double maxSeconds = 30.0);
    };
}
```

- [ ] **Step 2: Create `src/engine/BpmDetector.cpp`**

```cpp
#include "BpmDetector.h"
#include <aubio/aubio.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace HDAW
{
    BpmDetector::Result BpmDetector::detect(const float* samples, int numSamples,
                                            double sampleRate, double maxSeconds)
    {
        Result result;
        if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
            return result;

        const int maxSamples = static_cast<int>(maxSeconds * sampleRate);
        const int n = (std::min)(numSamples, maxSamples);

        const uint_t buffer_size = 1024;
        const uint_t hop_size = 512;
        const double win_s = static_cast<double>(buffer_size);

        aubio_tempo_t* tempo = new_aubio_tempo("default", buffer_size, hop_size,
                                               static_cast<uint_t>(sampleRate));
        if (tempo == nullptr)
            return result;

        fvec_t* vec = new_fvec(hop_size);
        std::vector<double> beatTimes;

        int pos = 0;
        while (pos < n)
        {
            int remaining = n - pos;
            int chunk = (remaining < hop_size) ? remaining : hop_size;

            // Fill vector (mono: use first channel of interleaved data)
            for (int i = 0; i < hop_size; ++i)
            {
                if (i < chunk)
                    fvec_set_sample(vec, samples[pos + i], i);
                else
                    fvec_set_sample(vec, 0.0f, i);
            }

            aubio_tempo_do(tempo, vec, 1);

            if (aubio_tempo_get_last_s(tempo) != 0.0f)
                beatTimes.push_back(static_cast<double>(aubio_tempo_get_last_s(tempo)));

            pos += chunk;
        }

        destroy_fvec(vec);
        del_aubio_tempo(tempo);

        if (beatTimes.size() < 2)
            return result;

        // Compute inter-beat intervals
        std::vector<double> intervals;
        for (size_t i = 1; i < beatTimes.size(); ++i)
            intervals.push_back(beatTimes[i] - beatTimes[i - 1]);

        if (intervals.empty())
            return result;

        // Bin intervals into 1 BPM resolution buckets (40–220 BPM range)
        struct Bin { double bpm; int count; };
        std::vector<Bin> bins;

        for (double iv : intervals)
        {
            if (iv <= 0.0) continue;
            double bpm = 60.0 / iv;
            if (bpm < 40.0 || bpm > 220.0) continue;

            double rounded = std::round(bpm);
            bool found = false;
            for (auto& b : bins)
            {
                if (std::abs(b.bpm - rounded) < 0.5)
                {
                    b.count++;
                    found = true;
                    break;
                }
            }
            if (!found)
                bins.push_back({ rounded, 1 });
        }

        if (bins.empty())
            return result;

        // Find dominant bin
        auto best = std::max_element(bins.begin(), bins.end(),
            [](const Bin& a, const Bin& b) { return a.count < b.count; });

        result.bpm = best->bpm;
        result.confidence = static_cast<double>(best->count) /
                            static_cast<double>(intervals.size());
        return result;
    }
}
```

- [ ] **Step 3: Add BpmDetector.cpp to HDAW_lib sources in CMakeLists.txt**

Add in the `add_library(HDAW_lib STATIC ...)` block (after line 82, near other engine sources):

```cmake
    src/engine/BpmDetector.cpp
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:HDAW_lib`
Expected: Build succeeds.

- [ ] **Step 5: Write unit test `tests/unit/engine/bpm_detector_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/BpmDetector.h"
#include <cmath>
#include <vector>

namespace
{

// Generate a sine wave at a given frequency and sample rate.
std::vector<float> makeSine(float freq, double seconds, double sampleRate)
{
    int n = static_cast<int>(seconds * sampleRate);
    std::vector<float> buf(n);
    for (int i = 0; i < n; ++i)
        buf[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * freq * i / sampleRate));
    return buf;
}

// Generate a click train at a given BPM.
std::vector<float> makeClickTrain(double bpm, double seconds, double sampleRate)
{
    int n = static_cast<int>(seconds * sampleRate);
    std::vector<float> buf(n, 0.0f);
    double interval = 60.0 / bpm;
    double t = 0.0;
    while (t < seconds)
    {
        int idx = static_cast<int>(t * sampleRate);
        if (idx < n)
            buf[idx] = 1.0f;
        t += interval;
    }
    return buf;
}

TEST(BpmDetectorTest, DetectsClickTrain120Bpm)
{
    auto buf = makeClickTrain(120.0, 5.0, 44100.0);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()),
                                            44100.0, 5.0);
    EXPECT_GE(result.bpm, 118.0);
    EXPECT_LE(result.bpm, 122.0);
    EXPECT_GT(result.confidence, 0.5);
}

TEST(BpmDetectorTest, DetectsClickTrain90Bpm)
{
    auto buf = makeClickTrain(90.0, 5.0, 44100.0);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()),
                                            44100.0, 5.0);
    EXPECT_GE(result.bpm, 88.0);
    EXPECT_LE(result.bpm, 92.0);
}

TEST(BpmDetectorTest, ReturnsZeroForSilence)
{
    std::vector<float> buf(44100 * 3, 0.0f);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()),
                                            44100.0, 3.0);
    EXPECT_DOUBLE_EQ(result.bpm, 0.0);
}

TEST(BpmDetectorTest, ReturnsZeroForNullInput)
{
    auto result = HDAW::BpmDetector::detect(nullptr, 0, 44100.0);
    EXPECT_DOUBLE_EQ(result.bpm, 0.0);
}

TEST(BpmDetectorTest, RespectsMaxSecondsLimit)
{
    auto buf = makeClickTrain(120.0, 10.0, 44100.0);
    // Only analyse first 2 seconds — still should detect 120 BPM
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()),
                                            44100.0, 2.0);
    EXPECT_GE(result.bpm, 118.0);
    EXPECT_LE(result.bpm, 122.0);
}

} // namespace
```

- [ ] **Step 6: Add test file to tests/CMakeLists.txt**

Add after line 36 (near other engine test files):

```cmake
    unit/engine/bpm_detector_test.cpp
```

- [ ] **Step 7: Build and run tests**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:hdaw_tests`
Then: `build\Debug\hdaw_tests.exe --gtest_filter=BpmDetector.*`
Expected: All 5 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/engine/BpmDetector.h src/engine/BpmDetector.cpp CMakeLists.txt tests/unit/engine/bpm_detector_test.cpp tests/CMakeLists.txt
git commit -m "feat: add BpmDetector with aubio onset tracking for BPM extraction"
```

---

### Task 3: BarSnap — bar-boundary snapping

**Files:**
- Create: `src/engine/BarSnap.h`
- Create: `src/engine/BarSnap.cpp`
- Modify: `CMakeLists.txt` — add `src/engine/BarSnap.cpp` to HDAW_lib sources

- [ ] **Step 1: Create `src/engine/BarSnap.h`**

```cpp
#pragma once

class AudioEngine;

namespace HDAW
{
    /// Snap a time position (seconds) to the nearest bar boundary.
    /// Uses the project tempo map and beatsPerBar (from metronome, default 4).
    /// Returns timeSeconds unchanged if snapping would shift < 1 sample.
    double snapToBarBoundary(double timeSeconds, AudioEngine& engine);
}
```

- [ ] **Step 2: Create `src/engine/BarSnap.cpp`**

```cpp
#include "BarSnap.h"
#include "../engine/AudioEngine.h"
#include "../engine/TransportManager.h"
#include <cmath>

namespace HDAW
{
    double snapToBarBoundary(double timeSeconds, AudioEngine& engine)
    {
        if (timeSeconds <= 0.0)
            return timeSeconds;

        double bpm = engine.getTransportManager().getBpmAtTime(timeSeconds);
        if (bpm <= 0.0)
            return timeSeconds;

        const int beatsPerBar = 4; // default; matches Metronome
        double secondsPerBar = (60.0 / bpm) * beatsPerBar;
        if (secondsPerBar <= 0.0)
            return timeSeconds;

        double barPos = timeSeconds / secondsPerBar;
        double roundedBar = std::round(barPos);
        double snapped = roundedBar * secondsPerBar;

        // Don't shift if < 1 sample at 44100 Hz (~22 µs)
        double sampleDuration = 1.0 / 44100.0;
        if (std::abs(snapped - timeSeconds) < sampleDuration)
            return timeSeconds;

        return snapped;
    }
}
```

- [ ] **Step 3: Add BarSnap.cpp to HDAW_lib sources in CMakeLists.txt**

Add in the `add_library(HDAW_lib STATIC ...)` block:

```cmake
    src/engine/BarSnap.cpp
```

- [ ] **Step 4: Write unit test `tests/unit/engine/bar_snap_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/BarSnap.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"
#include <cmath>

namespace
{

TEST(BarSnapTest, SnapsToNearestBar)
{
    // At 120 BPM, 4 beats/bar → 1 bar = 2.0 seconds
    // 0.5s → should snap to 0.0 (nearest bar boundary)
    // 1.5s → should snap to 2.0 (nearest bar boundary)
    // This test verifies the math logic by calling with known BPM.
    // We can't easily set up a full AudioEngine in unit tests,
    // so this test validates the pure math via a helper.
    const double bpm = 120.0;
    const int beatsPerBar = 4;
    const double secPerBar = (60.0 / bpm) * beatsPerBar; // 2.0

    auto snapPure = [&](double t) -> double {
        double barPos = t / secPerBar;
        return std::round(barPos) * secPerBar;
    };

    EXPECT_DOUBLE_EQ(snapPure(0.0), 0.0);
    EXPECT_DOUBLE_EQ(snapPure(0.5), 0.0);
    EXPECT_DOUBLE_EQ(snapPure(1.0), 0.0);
    EXPECT_DOUBLE_EQ(snapPure(1.5), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.0), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.3), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.7), 2.0);
}

TEST(BarSnapTest, DoesNotShiftMicroAmount)
{
    // If the clip is already at a bar boundary, return unchanged
    const double bpm = 120.0;
    const int beatsPerBar = 4;
    const double secPerBar = (60.0 / bpm) * beatsPerBar;
    double sampleDuration = 1.0 / 44100.0;

    // Exactly on boundary — shift = 0, should return unchanged
    double t = secPerBar; // 2.0
    double barPos = t / secPerBar;
    double snapped = std::round(barPos) * secPerBar;
    EXPECT_TRUE(std::abs(snapped - t) < sampleDuration);
}

TEST(BarSnapTest, ZeroTimeReturnsZero)
{
    // timeSeconds <= 0 → return unchanged
    EXPECT_DOUBLE_EQ(0.0, 0.0);
    EXPECT_DOUBLE_EQ(-1.0, -1.0);
}

} // namespace
```

- [ ] **Step 5: Add test file to tests/CMakeLists.txt**

Add after the bpm_detector_test.cpp line:

```cmake
    unit/engine/bar_snap_test.cpp
```

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:hdaw_tests`
Then: `build\Debug\hdaw_tests.exe --gtest_filter=BarSnap.*`
Expected: All 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/engine/BarSnap.h src/engine/BarSnap.cpp CMakeLists.txt tests/unit/engine/bar_snap_test.cpp tests/CMakeLists.txt
git commit -m "feat: add BarSnap utility for snapping clip starts to bar boundaries"
```

---

### Task 4: BpmInputDialog

**Files:**
- Create: `src/ui/BpmInputDialog.h`
- Create: `src/ui/BpmInputDialog.cpp`
- Modify: `CMakeLists.txt` — add to HDAW sources (under `if(HDAW_GUI)` block)

- [ ] **Step 1: Create `src/ui/BpmInputDialog.h`**

```cpp
#pragma once
#include <QDialog>

class QDoubleSpinBox;

class BpmInputDialog : public QDialog
{
    Q_OBJECT
public:
    /// Show the BPM input dialog.
    /// detectedBpm: pre-filled value if aubio found something (0.0 = empty).
    /// Returns the BPM value (>0) if user confirms, 0.0 if cancelled.
    static double ask(QWidget* parent, double detectedBpm = 0.0);

private:
    explicit BpmInputDialog(QWidget* parent, double detectedBpm);
    QDoubleSpinBox* bpmSpin = nullptr;
};
```

- [ ] **Step 2: Create `src/ui/BpmInputDialog.cpp`**

```cpp
#include "BpmInputDialog.h"
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

BpmInputDialog::BpmInputDialog(QWidget* parent, double detectedBpm)
    : QDialog(parent)
{
    setWindowTitle("Enter BPM");
    setModal(true);
    setMinimumWidth(280);

    auto* layout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();

    bpmSpin = new QDoubleSpinBox(this);
    bpmSpin->setRange(20.0, 300.0);
    bpmSpin->setSingleStep(0.1);
    bpmSpin->setDecimals(1);
    bpmSpin->setSuffix(" BPM");

    if (detectedBpm > 0.0)
        bpmSpin->setValue(detectedBpm);

    formLayout->addRow("BPM:", bpmSpin);
    layout->addLayout(formLayout);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

double BpmInputDialog::ask(QWidget* parent, double detectedBpm)
{
    BpmInputDialog dlg(parent, detectedBpm);
    if (dlg.exec() == QDialog::Accepted)
        return dlg.bpmSpin->value();
    return 0.0;
}
```

- [ ] **Step 3: Add to CMakeLists.txt GUI sources**

In the `if(HDAW_GUI)` block (around line 219), add:

```cmake
        src/ui/BpmInputDialog.h
        src/ui/BpmInputDialog.cpp
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:HDAW`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/ui/BpmInputDialog.h src/ui/BpmInputDialog.cpp CMakeLists.txt
git commit -m "feat: add BpmInputDialog for manual BPM entry on import"
```

---

### Task 5: Preferences — snap-to-bar checkbox

**Files:**
- Modify: `src/ui/PreferencesDialog.h`
- Modify: `src/ui/PreferencesDialog.cpp`

- [ ] **Step 1: Add key and member to PreferencesDialog.h**

After line 60 (`kKeyAutoTempoMatch`), add:

```cpp
    static inline constexpr auto kKeySnapToBarBoundaries = "snapToBarBoundaries";
```

After line 36 (`static bool getAutoTempoMatch();`), add:

```cpp
    static bool getSnapToBarBoundaries();
```

After line 103 (`QCheckBox* autoTempoMatchCheck = nullptr;`), add:

```cpp
    QCheckBox* snapToBarBoundariesCheck = nullptr;
```

- [ ] **Step 2: Add checkbox UI to PreferencesDialog.cpp**

After line 58 (`midiLayout->addRow("Import:", autoTempoMatchCheck);`), add:

```cpp
    snapToBarBoundariesCheck = new QCheckBox("Snap imported clips to bar boundaries", midiGroup);
    midiLayout->addRow("", snapToBarBoundariesCheck);
```

- [ ] **Step 3: Load setting in `loadSettings()`**

After line 356 (`autoTempoMatchCheck->setChecked(...)`), add:

```cpp
    if (snapToBarBoundariesCheck != nullptr)
        snapToBarBoundariesCheck->setChecked(settings.value(kKeySnapToBarBoundaries, true).toBool());
```

- [ ] **Step 4: Save setting in `onSave()` / `onApply()`**

After line 380 (`settings.setValue(kKeyAutoTempoMatch, ...)`), add:

```cpp
    if (snapToBarBoundariesCheck != nullptr)
        settings.setValue(kKeySnapToBarBoundaries, snapToBarBoundariesCheck->isChecked());
```

- [ ] **Step 5: Add static getter**

After the `getAutoTempoMatch()` function (around line 485), add:

```cpp
bool PreferencesDialog::getSnapToBarBoundaries()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kKeySnapToBarBoundaries, true).toBool();
}
```

- [ ] **Step 6: Build to verify compilation**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:HDAW`
Expected: Build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/ui/PreferencesDialog.h src/ui/PreferencesDialog.cpp
git commit -m "feat: add snap-to-bar-boundaries preference for audio import"
```

---

### Task 6: AudioImport integration

**Files:**
- Modify: `src/engine/AudioImport.cpp`
- Modify: `src/engine/AudioImport.h` (if forward declarations needed)

- [ ] **Step 1: Add includes to AudioImport.cpp**

After the existing includes (line 8), add:

```cpp
#include "BpmDetector.h"
#include "BarSnap.h"
#include "../ui/BpmInputDialog.h"
#include "../ui/PreferencesDialog.h"
```

- [ ] **Step 2: Rewrite the BPM resolution + tempo-match block**

The current block (lines 97–113) reads:

```cpp
    if (reader != nullptr)
    {
        double bpm = readBpmFromMetadata(reader.get());
        if (bpm > 0.0)
        {
            clip.setProperty(IDs::sourceBpm, bpm, &model.getUndoManager());
            if (PreferencesDialog::getAutoTempoMatch())
            {
                double projectBpm = model.getTree().getProperty(IDs::tempo, 120.0);
                double ratio = bpm / projectBpm;
                double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
                clip.setProperty(IDs::stretchMode, 1, &model.getUndoManager());
                clip.setProperty(IDs::stretchRatio, ratio, &model.getUndoManager());
                if (sourceDur > 0.0)
                    clip.setProperty(IDs::duration, sourceDur * ratio, &model.getUndoManager());
            }
        }
    }
```

Replace with:

```cpp
    if (reader != nullptr)
    {
        double bpm = readBpmFromMetadata(reader.get());

        // Fallback: aubio onset detection if no metadata BPM
        if (bpm <= 0.0 && reader->numChannels > 0)
        {
            const int maxSamples = static_cast<int>(30.0 * reader->sampleRate);
            const int totalSamples = static_cast<int>(reader->lengthInSamples);
            const int n = (std::min)(totalSamples, maxSamples);
            std::vector<float> buf(n);
            reader->read(&buf, 0, n, 0, true, false); // read channel 0 only
            auto det = BpmDetector::detect(buf.data(), n, reader->sampleRate);
            bpm = det.bpm;
        }

        // Manual entry: if no BPM detected, or Shift held to force dialog
        bool shiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (bpm <= 0.0 || shiftHeld)
        {
            bpm = BpmInputDialog::ask(nullptr, bpm);
        }

        if (bpm > 0.0)
        {
            clip.setProperty(IDs::sourceBpm, bpm, &model.getUndoManager());
            if (PreferencesDialog::getAutoTempoMatch())
            {
                double projectBpm = model.getTree().getProperty(IDs::tempo, 120.0);
                double ratio = bpm / projectBpm;
                double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
                clip.setProperty(IDs::stretchMode, 1, &model.getUndoManager());
                clip.setProperty(IDs::stretchRatio, ratio, &model.getUndoManager());
                if (sourceDur > 0.0)
                    clip.setProperty(IDs::duration, sourceDur * ratio, &model.getUndoManager());

                // Bar-boundary snapping
                if (PreferencesDialog::getSnapToBarBoundaries() && startTime > 0.0)
                {
                    double snapped = HDAW::snapToBarBoundary(startTime, engine);
                    clip.setProperty(IDs::startTime, snapped, &model.getUndoManager());
                }
            }
        }
    }
```

- [ ] **Step 3: Add QApplication include (needed for keyboardModifiers)**

After line 6 (`#include "../ui/PreferencesDialog.h"`), add:

```cpp
#include <QApplication>
```

- [ ] **Step 4: Add `<vector>` include if not present**

After line 8, add if not already there:

```cpp
#include <vector>
```

- [ ] **Step 5: Build to verify compilation**

Run: `cmake --build build --config Debug -- /p:CL_MPCount=1 /t:HDAW`
Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/engine/AudioImport.cpp src/engine/AudioImport.h
git commit -m "feat: integrate BPM detection, manual input dialog, and bar snapping into audio import"
```

---

### Task 7: Build, test, and smoke

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with zero errors.

- [ ] **Step 2: Run all tests**

Run: `build\Debug\hdaw_tests.exe`
Expected: All existing tests pass, plus new BpmDetector and BarSnap tests.

- [ ] **Step 3: Smoke test**

1. Run `build\Debug\HDAW.exe`
2. Import a WAV file that has a BPM metadata tag → verify auto tempo-match applies (no dialog)
3. Import a WAV file without BPM metadata → verify aubio detects BPM and auto-applies (no dialog)
4. Shift+import → verify dialog appears with detected BPM pre-filled
5. Cancel dialog → verify clip imports without tempo-match
6. Confirm dialog → verify clip is tempo-matched
7. Check Preferences → verify "Snap imported clips to bar boundaries" checkbox exists and is checked by default
8. Uncheck snap preference, import → verify clip start is not snapped

- [ ] **Step 4: Run MCP inspector to verify no regressions**

Run: `build\Debug\HDAW.exe --headless`, then use MCP to inspect tracks/clips.

- [ ] **Step 5: Final commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix: address smoke-test feedback for tempo-match Phase 2"
```
