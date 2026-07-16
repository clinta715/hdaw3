# Clip Gain Envelope & Slicing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-clip gain envelope editing and clip slicing (at playhead / at transients) to the Audio Clip Editor.

**Architecture:** 
- Gain envelope stored as `GAIN_ENVELOPE` child under `CLIP` with `GAIN_ENVELOPE_POINT` children (time, gain). Processed in `ClipSourceProcessor::processBlock` via linear interpolation.
- Slicing creates new clips on same track via `ProjectModel::createAudioClip`. Transient detection runs off-thread in `StretchRenderer` worker thread pattern.

**Tech Stack:** JUCE 8, Qt 6, C++17, gtest, existing ValueTree/UndoManager patterns.

---

### Task 1: Add ValueTree IDs for Gain Envelope

**Files:**
- Modify: `src/model/ProjectModel.h` (add IDs)
- Test: `tests/unit/model/project_model_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include "gtest/gtest.h"
#include "../src/model/ProjectModel.h"
#include "../src/model/IDs.h"

TEST(ProjectModelIDs, GainEnvelopeIDsExist)
{
    // IDs should be defined in IDs namespace
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE_POINT).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointTime).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointGain).isValid());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ProjectModelIDs.GainEnvelopeIDsExist
```
Expected: FAIL (identifiers not defined)

- [ ] **Step 3: Add IDs to ProjectModel.h**

```cpp
// In IDs namespace, add:
static const juce::Identifier GAIN_ENVELOPE { "GAIN_ENVELOPE" };
static const juce::Identifier GAIN_ENVELOPE_POINT { "GAIN_ENVELOPE_POINT" };
static const juce::Identifier pointTime { "pointTime" };
static const juce::Identifier pointGain { "pointGain" };
```

- [ ] **Step 4: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ProjectModelIDs.GainEnvelopeIDsExist
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/model/ProjectModel.h tests/unit/model/project_model_test.cpp
git commit -m "model: add GainEnvelope ValueTree IDs"
```

---

### Task 2: Gain Envelope ValueTree Helpers in ProjectModel

**Files:**
- Modify: `src/model/ProjectModel.h` (declare), `src/model/ProjectModel.cpp` (implement)
- Test: `tests/unit/model/project_model_test.cpp`

- [ ] **Step 1: Write failing tests for helpers**

```cpp
TEST(ProjectModel, CreateGainEnvelopeForClip)
{
    HDAW::ProjectModel model;
    auto clip = HDAW::ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    
    auto envelope = HDAW::ProjectModel::ensureGainEnvelope(clip);
    EXPECT_TRUE(envelope.isValid());
    EXPECT_TRUE(envelope.hasType(IDs::GAIN_ENVELOPE));
    EXPECT_EQ(clip.getChildWithName(IDs::GAIN_ENVELOPE), envelope);
}

TEST(ProjectModel, AddGainEnvelopePoint)
{
    HDAW::ProjectModel model;
    auto clip = HDAW::ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = HDAW::ProjectModel::ensureGainEnvelope(clip);
    
    auto point = HDAW::ProjectModel::addGainEnvelopePoint(envelope, 1.0, 0.5, nullptr);
    EXPECT_TRUE(point.isValid());
    EXPECT_EQ(point.getProperty(IDs::pointTime), 1.0);
    EXPECT_EQ(point.getProperty(IDs::pointGain), 0.5);
}

TEST(ProjectModel, GetGainEnvelopePoints)
{
    HDAW::ProjectModel model;
    auto clip = HDAW::ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = HDAW::ProjectModel::ensureGainEnvelope(clip);
    
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 0.0, 1.0, nullptr);
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 2.0, 0.5, nullptr);
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 4.0, 1.0, nullptr);
    
    auto points = HDAW::ProjectModel::getGainEnvelopePoints(envelope);
    EXPECT_EQ(points.size(), 3);
    EXPECT_DOUBLE_EQ(points[0].time, 0.0);
    EXPECT_DOUBLE_EQ(points[0].gain, 1.0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
build/Debug/hdaw_tests.exe --gtest_filter="ProjectModel.CreateGainEnvelopeForClip:ProjectModel.AddGainEnvelopePoint:ProjectModel.GetGainEnvelopePoints"
```
Expected: FAIL (functions not declared)

- [ ] **Step 3: Implement helpers in ProjectModel.h/.cpp**

```cpp
// In ProjectModel.h, public section:
struct GainEnvelopePoint { double time; double gain; };

static juce::ValueTree ensureGainEnvelope(juce::ValueTree clip);
static juce::ValueTree addGainEnvelopePoint(juce::ValueTree envelope, double time, double gain, juce::UndoManager* um);
static std::vector<GainEnvelopePoint> getGainEnvelopePoints(const juce::ValueTree& envelope);
static void removeGainEnvelopePoint(juce::ValueTree envelope, int index, juce::UndoManager* um);
static void clearGainEnvelope(juce::ValueTree envelope, juce::UndoManager* um);
```

```cpp
// In ProjectModel.cpp:
juce::ValueTree ProjectModel::ensureGainEnvelope(juce::ValueTree clip)
{
    if (!clip.isValid()) return {};
    auto env = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!env.isValid())
    {
        env = juce::ValueTree(IDs::GAIN_ENVELOPE);
        clip.addChild(env, -1, nullptr);
    }
    return env;
}

juce::ValueTree ProjectModel::addGainEnvelopePoint(juce::ValueTree envelope, double time, double gain, juce::UndoManager* um)
{
    if (!envelope.isValid() || !envelope.hasType(IDs::GAIN_ENVELOPE)) return {};
    juce::ValueTree point(IDs::GAIN_ENVELOPE_POINT);
    point.setProperty(IDs::pointTime, time, um);
    point.setProperty(IDs::pointGain, gain, um);
    // Insert sorted by time
    int insertIdx = 0;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        if (envelope.getChild(i).getProperty(IDs::pointTime) < time)
            insertIdx = i + 1;
        else
            break;
    }
    envelope.addChild(point, insertIdx, um);
    return point;
}

std::vector<ProjectModel::GainEnvelopePoint> ProjectModel::getGainEnvelopePoints(const juce::ValueTree& envelope)
{
    std::vector<GainEnvelopePoint> result;
    if (!envelope.isValid()) return result;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        auto child = envelope.getChild(i);
        if (child.hasType(IDs::GAIN_ENVELOPE_POINT))
        {
            result.push_back({ child.getProperty(IDs::pointTime), child.getProperty(IDs::pointGain) });
        }
    }
    return result;
}

void ProjectModel::removeGainEnvelopePoint(juce::ValueTree envelope, int index, juce::UndoManager* um)
{
    if (!envelope.isValid() || index < 0 || index >= envelope.getNumChildren()) return;
    envelope.removeChild(index, um);
}

void ProjectModel::clearGainEnvelope(juce::ValueTree envelope, juce::UndoManager* um)
{
    if (!envelope.isValid()) return;
    envelope.removeAllChildren(um);
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
build/Debug/hdaw_tests.exe --gtest_filter="ProjectModel.CreateGainEnvelopeForClip:ProjectModel.AddGainEnvelopePoint:ProjectModel.GetGainEnvelopePoints"
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/model/ProjectModel.h src/model/ProjectModel.cpp tests/unit/model/project_model_test.cpp
git commit -m "model: add GainEnvelope ValueTree helpers"
```

---

### Task 3: ClipSourceProcessor - Apply Gain Envelope in processBlock

**Files:**
- Modify: `src/engine/ClipSourceProcessor.h` (add envelope cache), `src/engine/ClipSourceProcessor.cpp` (implement)
- Test: `tests/unit/engine/clip_source_processor_test.cpp`

- [ ] **Step 1: Write failing test for envelope interpolation**

```cpp
#include "gtest/gtest.h"
#include "../src/engine/ClipSourceProcessor.h"
#include "../src/model/ProjectModel.h"

TEST(ClipSourceProcessor, GainEnvelopeInterpolation)
{
    HDAW::ProjectModel model;
    auto clip = HDAW::ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = HDAW::ProjectModel::ensureGainEnvelope(clip);
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 0.0, 1.0, nullptr);
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 2.0, 0.5, nullptr);
    HDAW::ProjectModel::addGainEnvelopePoint(envelope, 4.0, 1.0, nullptr);
    
    // Create processor and set envelope
    HDAW::TransportManager tm;
    juce::AudioFormatManager fm;
    HDAW::ClipSourceProcessor proc(tm, fm);
    proc.setSourceFile("dummy.wav");
    proc.prepareToPlay(44100.0, 512);
    
    // Use reflection or public API to inject envelope points
    proc.setGainEnvelopePoints({ {0.0, 1.0}, {2.0, 0.5}, {4.0, 1.0} });
    
    // Test interpolation at various times
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(0.0), 1.0);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(1.0), 0.75);  // linear between 1.0 and 0.5
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(2.0), 0.5);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(3.0), 0.75);  // linear between 0.5 and 1.0
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(4.0), 1.0);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(5.0), 1.0);   // hold last value
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ClipSourceProcessor.GainEnvelopeInterpolation
```
Expected: FAIL (methods don't exist)

- [ ] **Step 3: Add envelope support to ClipSourceProcessor**

```cpp
// In ClipSourceProcessor.h, add:
struct GainPoint { double time; double gain; };
void setGainEnvelopePoints(const std::vector<GainPoint>& points);
double getGainAtTime(double time) const;

// Private members:
mutable std::vector<GainPoint> gainEnvelopePoints;
mutable std::mutex envelopeMutex;  // for thread-safe updates from UI thread
```

```cpp
// In ClipSourceProcessor.cpp:
void ClipSourceProcessor::setGainEnvelopePoints(const std::vector<GainPoint>& points)
{
    std::lock_guard<std::mutex> lock(envelopeMutex);
    gainEnvelopePoints = points;
}

double ClipSourceProcessor::getGainAtTime(double time) const
{
    std::lock_guard<std::mutex> lock(envelopeMutex);
    if (gainEnvelopePoints.empty()) return 1.0;
    if (gainEnvelopePoints.size() == 1) return gainEnvelopePoints[0].gain;
    
    // Find surrounding points
    for (size_t i = 0; i < gainEnvelopePoints.size() - 1; ++i)
    {
        double t0 = gainEnvelopePoints[i].time;
        double t1 = gainEnvelopePoints[i + 1].time;
        if (time >= t0 && time <= t1)
        {
            double g0 = gainEnvelopePoints[i].gain;
            double g1 = gainEnvelopePoints[i + 1].gain;
            double alpha = (time - t0) / (t1 - t0);
            return g0 + alpha * (g1 - g0);
        }
    }
    // Before first or after last - hold edge values
    if (time < gainEnvelopePoints.front().time) return gainEnvelopePoints.front().gain;
    return gainEnvelopePoints.back().gain;
}
```

- [ ] **Step 4: Integrate envelope gain into processBlock**

```cpp
// In processBlock, after existing gain envelope (fadeIn/fadeOut), multiply by envelope gain:
double clipLocalTime = clipLocalSample / sr;
double envGain = getGainAtTime(clipLocalTime);
// Apply to each sample:
channelData[s] *= static_cast<float>(envGain);
```

- [ ] **Step 5: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ClipSourceProcessor.GainEnvelopeInterpolation
```
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/engine/ClipSourceProcessor.h src/engine/ClipSourceProcessor.cpp tests/unit/engine/clip_source_processor_test.cpp
git commit -m "engine: add gain envelope support to ClipSourceProcessor"
```

---

### Task 4: AudioClipEditorWidget - Gain Envelope UI

**Files:**
- Modify: `src/ui/AudioClipEditorWidget.h` (add envelope editor), `src/ui/AudioClipEditorWidget.cpp` (implement)
- Create: `src/ui/GainEnvelopeEditor.h`, `src/ui/GainEnvelopeEditor.cpp` (new widget)
- Test: `tests/unit/ui/audio_clip_editor_widget_test.cpp` (UI tests minimal, focus on integration)

- [ ] **Step 1: Create GainEnvelopeEditor widget**

```cpp
// GainEnvelopeEditor.h
#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>

class GainEnvelopeEditor : public QWidget
{
    Q_OBJECT
public:
    struct Point { double time; double gain; };
    
    explicit GainEnvelopeEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<Point>& points);
    void setDuration(double seconds) { duration = seconds; update(); }
    QVector<Point> getPoints() const;
    
signals:
    void pointsChanged(const QVector<Point>& points);
    void pointAdded(double time, double gain);
    void pointRemoved(int index);
    void pointMoved(int index, double time, double gain);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QVector<Point> points;
    double duration = 4.0;
    int dragIndex = -1;
    bool adding = false;
    
    int timeToX(double t) const { return static_cast<int>(t / duration * width()); }
    double xToTime(int x) const { return static_cast<double>(x) / width() * duration; }
    int gainToY(double g) const { return height() - static_cast<int>(g * height()); }
    double yToGain(int y) const { return 1.0 - static_cast<double>(y) / height(); }
    int hitTest(const QPoint& pos) const;
};
```

```cpp
// GainEnvelopeEditor.cpp - key implementation
void GainEnvelopeEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 35));
    
    // Grid
    p.setPen(QColor(60, 60, 70));
    for (int i = 1; i < 4; ++i)
        p.drawLine(0, height() * i / 4, width(), height() * i / 4);
    
    // Envelope curve
    if (points.size() >= 2)
    {
        QPainterPath path;
        path.moveTo(timeToX(points[0].time), gainToY(points[0].gain));
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(timeToX(points[i].time), gainToY(points[i].gain));
        p.setPen(QPen(QColor(0x06, 0xb6, 0xd4), 2));
        p.drawPath(path);
    }
    
    // Points
    p.setBrush(QColor(0x06, 0xb6, 0xd4));
    for (const auto& pt : points)
    {
        int x = timeToX(pt.time);
        int y = gainToY(pt.gain);
        p.drawEllipse(QPoint(x, y), 5, 5);
    }
    
    // Drag preview
    if (dragIndex >= 0 && dragIndex < points.size())
    {
        int x = timeToX(points[dragIndex].time);
        int y = gainToY(points[dragIndex].gain);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(QPoint(x, y), 8, 8);
    }
}

int GainEnvelopeEditor::hitTest(const QPoint& pos) const
{
    for (int i = 0; i < points.size(); ++i)
    {
        int x = timeToX(points[i].time);
        int y = gainToY(points[i].gain);
        if (QPoint(x, y).manhattanLength() < 10) return i;
    }
    return -1;
}

void GainEnvelopeEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    int idx = hitTest(e->pos());
    if (idx >= 0)
    {
        dragIndex = idx;
    }
    else
    {
        // Add new point
        double t = std::clamp(xToTime(e->x()), 0.0, duration);
        double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
        points.append({t, g});
        std::sort(points.begin(), points.end(), [](auto a, auto b){ return a.time < b.time; });
        dragIndex = points.indexOf({t, g});
        adding = true;
        emit pointAdded(t, g);
    }
    update();
}

void GainEnvelopeEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (dragIndex < 0) return;
    double t = std::clamp(xToTime(e->x()), 0.0, duration);
    double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
    points[dragIndex] = {t, g};
    std::sort(points.begin(), points.end(), [](auto a, auto b){ return a.time < b.time; });
    dragIndex = points.indexOf({t, g});
    emit pointMoved(dragIndex, t, g);
    emit pointsChanged(points);
    update();
}

void GainEnvelopeEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && dragIndex >= 0)
    {
        if (adding)
        {
            // If point didn't move much, keep it
            adding = false;
        }
        dragIndex = -1;
    }
    else if (e->button() == Qt::RightButton)
    {
        int idx = hitTest(e->pos());
        if (idx >= 0 && points.size() > 2)
        {
            points.remove(idx);
            emit pointRemoved(idx);
            emit pointsChanged(points);
            update();
        }
    }
}
```

- [ ] **Step 2: Integrate into AudioClipEditorWidget**

```cpp
// In AudioClipEditorWidget.h, add:
GainEnvelopeEditor* gainEnvelopeEditor = nullptr;
void loadGainEnvelope();
void onGainEnvelopeChanged(const QVector<GainEnvelopeEditor::Point>& points);
```

```cpp
// In AudioClipEditorWidget.cpp setupUI(), add envelope editor tab or section:
gainEnvelopeEditor = new GainEnvelopeEditor(this);
connect(gainEnvelopeEditor, &GainEnvelopeEditor::pointsChanged, this, &AudioClipEditorWidget::onGainEnvelopeChanged);
// Add to layout (e.g., new tab in bottom stack, or collapsible section)
```

```cpp
// In loadClip():
void AudioClipEditorWidget::loadGainEnvelope()
{
    if (!currentClip.isValid()) return;
    auto envelope = currentClip.getChildWithName(IDs::GAIN_ENVELOPE);
    QVector<GainEnvelopeEditor::Point> points;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        auto pt = envelope.getChild(i);
        points.append({ pt.getProperty(IDs::pointTime), pt.getProperty(IDs::pointGain) });
    }
    gainEnvelopeEditor->setPoints(points);
    gainEnvelopeEditor->setDuration(currentClip.getProperty(IDs::duration));
}

void AudioClipEditorWidget::onGainEnvelopeChanged(const QVector<GainEnvelopeEditor::Point>& points)
{
    if (settingUi || !currentClip.isValid()) return;
    int clipId = currentClip.getProperty(IDs::clipID);
    auto envelope = ProjectModel::ensureGainEnvelope(currentClip);
    ProjectModel::clearGainEnvelope(envelope, &model.getUndoManager());
    for (const auto& pt : points)
    {
        ProjectModel::addGainEnvelopePoint(envelope, pt.time, pt.gain, &model.getUndoManager());
    }
    // Update processor
    engine.getMainProcessor()->updateClipGainEnvelope(clipId, points.toStdVector());
}
```

- [ ] **Step 3: Add updateClipGainEnvelope to MainAudioProcessor**

```cpp
// MainAudioProcessor.h:
void updateClipGainEnvelope(int clipId, const std::vector<ClipSourceProcessor::GainPoint>& points);
```

```cpp
// MainAudioProcessor.cpp:
void MainAudioProcessor::updateClipGainEnvelope(int clipId, const std::vector<ClipSourceProcessor::GainPoint>& points)
{
    auto* rm = getRoutingManager();
    if (!rm) return;
    // Find clip processor by clipId and call setGainEnvelopePoints
    for (auto& kv : rm->getAudioClipSources())
    {
        if (kv.second->getClipID() == clipId)
        {
            kv.second->setGainEnvelopePoints(points);
            break;
        }
    }
}
```

- [ ] **Step 4: Build and verify compiles**

```bash
cmake --build build --config Debug --target HDAW
```
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/ui/GainEnvelopeEditor.h src/ui/GainEnvelopeEditor.cpp src/ui/AudioClipEditorWidget.h src/ui/AudioClipEditorWidget.cpp src/engine/MainAudioProcessor.h src/engine/MainAudioProcessor.cpp
git commit -m "ui: add gain envelope editor to AudioClipEditorWidget"
```

---

### Task 5: ProjectCommands - Gain Envelope Commands

**Files:**
- Modify: `src/common/ProjectCommands.h` (declare), `src/common/ProjectCommands.cpp` (implement)
- Modify: `src/engine/AudioEngineCommands.cpp` (implement)
- Test: `tests/integration/commands/gain_envelope_commands_test.cpp`

- [ ] **Step 1: Write failing integration test**

```cpp
TEST(ProjectCommands, GainEnvelopeCRUD)
{
    HDAW::AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    
    // Create clip
    int clipId = 0;
    cmds.createAudioClip("test.wav", 0.0, 4.0, 0, [&](int id) { clipId = id; });
    
    // Add points
    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);
    cmds.addGainEnvelopePoint(clipId, 4.0, 1.0);
    
    // Verify
    auto trackList = model.getTrackListTree();
    auto clip = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST).getChild(0);
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    EXPECT_EQ(envelope.getNumChildren(), 3);
    
    // Move point
    cmds.moveGainEnvelopePoint(clipId, 1, 2.5, 0.3);
    auto pt = envelope.getChild(1);
    EXPECT_DOUBLE_EQ(pt.getProperty(IDs::pointTime), 2.5);
    EXPECT_DOUBLE_EQ(pt.getProperty(IDs::pointGain), 0.3);
    
    // Remove point
    cmds.removeGainEnvelopePoint(clipId, 1);
    EXPECT_EQ(envelope.getNumChildren(), 2);
    
    // Clear
    cmds.clearGainEnvelope(clipId);
    EXPECT_FALSE(clip.getChildWithName(IDs::GAIN_ENVELOPE).isValid());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ProjectCommands.GainEnvelopeCRUD
```
Expected: FAIL

- [ ] **Step 3: Add commands to ProjectCommands.h/.cpp**

```cpp
// ProjectCommands.h:
virtual void addGainEnvelopePoint(int clipId, double time, double gain) = 0;
virtual void moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain) = 0;
virtual void removeGainEnvelopePoint(int clipId, int pointIndex) = 0;
virtual void clearGainEnvelope(int clipId) = 0;
```

```cpp
// AudioEngineCommands.cpp:
void AudioEngineCommands::addGainEnvelopePoint(int clipId, double time, double gain)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    auto envelope = ProjectModel::ensureGainEnvelope(clip);
    ProjectModel::addGainEnvelopePoint(envelope, time, gain, &projectModel.getUndoManager());
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!envelope.isValid() || pointIndex < 0 || pointIndex >= envelope.getNumChildren()) return;
    auto pt = envelope.getChild(pointIndex);
    pt.setProperty(IDs::pointTime, time, &projectModel.getUndoManager());
    pt.setProperty(IDs::pointGain, gain, &projectModel.getUndoManager());
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::removeGainEnvelopePoint(int clipId, int pointIndex)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    ProjectModel::removeGainEnvelopePoint(envelope, pointIndex, &projectModel.getUndoManager());
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::clearGainEnvelope(int clipId)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    clip.removeChildWithName(IDs::GAIN_ENVELOPE, &projectModel.getUndoManager());
    notifyClipGainEnvelopeChanged(clipId);
}
```

- [ ] **Step 4: Add notifyClipGainEnvelopeChanged to rebuild processor envelope**

```cpp
void AudioEngineCommands::notifyClipGainEnvelopeChanged(int clipId)
{
    auto* proc = mainProcessor.get();
    if (proc) proc->updateClipGainEnvelope(clipId, getGainEnvelopePoints(clipId));
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ProjectCommands.GainEnvelopeCRUD
```
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/common/ProjectCommands.h src/common/ProjectCommands.cpp src/engine/AudioEngineCommands.cpp tests/integration/commands/gain_envelope_commands_test.cpp
git commit -m "commands: add gain envelope CRUD operations"
```

---

### Task 6: Clip Slicing - Core Logic

**Files:**
- Modify: `src/model/ProjectModel.h/.cpp` (add slicing methods)
- Test: `tests/unit/model/clip_slicing_test.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(ProjectModel, SliceClipAtTime)
{
    HDAW::ProjectModel model;
    auto track = model.getTrackListTree().getChild(0);  // assume default track exists
    auto clip = HDAW::ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, &model.getUndoManager());
    int clipId = clip.getProperty(IDs::clipID);
    
    // Slice at 1.0 and 3.0
    auto slices = HDAW::ProjectModel::sliceClipAtTimes(clip, {1.0, 3.0}, &model.getUndoManager());
    
    EXPECT_EQ(slices.size(), 3);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::startTime), 0.0);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::duration), 1.0);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::offset), 0.0);
    
    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::startTime), 1.0);
    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::duration), 2.0);
    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::offset), 1.0);
    
    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::startTime), 3.0);
    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::duration), 1.0);
    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::offset), 3.0);
    
    // Original clip should be removed
    EXPECT_EQ(track.getChildWithName(IDs::CLIP_LIST).getNumChildren(), 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ProjectModel.SliceClipAtTime
```
Expected: FAIL

- [ ] **Step 3: Implement sliceClipAtTimes**

```cpp
// ProjectModel.h:
static std::vector<juce::ValueTree> sliceClipAtTimes(juce::ValueTree clip, const std::vector<double>& times, juce::UndoManager* um);

// ProjectModel.cpp:
std::vector<juce::ValueTree> ProjectModel::sliceClipAtTimes(juce::ValueTree clip, const std::vector<double>& times, juce::UndoManager* um)
{
    std::vector<juce::ValueTree> result;
    if (!clip.isValid() || !clip.hasType(IDs::CLIP)) return result;
    
    double clipStart = clip.getProperty(IDs::startTime);
    double clipDur = clip.getProperty(IDs::duration);
    double clipOffset = clip.getProperty(IDs::offset);
    double clipEnd = clipStart + clipDur;
    
    // Filter and sort valid slice times
    std::vector<double> validTimes;
    for (double t : times)
    {
        if (t > clipStart && t < clipEnd)
            validTimes.push_back(t);
    }
    std::sort(validTimes.begin(), validTimes.end());
    validTimes.erase(std::unique(validTimes.begin(), validTimes.end()), validTimes.end());
    
    if (validTimes.empty()) return result;
    
    auto clipList = clip.getParent();  // CLIP_LIST
    int clipIndex = clipList.indexOf(clip);
    
    double prevTime = clipStart;
    double prevOffset = clipOffset;
    int newIndex = clipIndex;
    
    for (double sliceTime : validTimes)
    {
        double sliceDur = sliceTime - prevTime;
        auto newClip = clip.createCopy();
        newClip.setProperty(IDs::startTime, prevTime, um);
        newClip.setProperty(IDs::duration, sliceDur, um);
        newClip.setProperty(IDs::offset, prevOffset, um);
        newClip.setProperty(IDs::clipID, getNextClipId(), um);  // new unique ID
        clipList.addChild(newClip, newIndex++, um);
        result.push_back(newClip);
        
        prevTime = sliceTime;
        prevOffset += sliceDur;
    }
    
    // Final slice
    double finalDur = clipEnd - prevTime;
    auto finalClip = clip.createCopy();
    finalClip.setProperty(IDs::startTime, prevTime, um);
    finalClip.setProperty(IDs::duration, finalDur, um);
    finalClip.setProperty(IDs::offset, prevOffset, um);
    finalClip.setProperty(IDs::clipID, getNextClipId(), um);
    clipList.addChild(finalClip, newIndex++, um);
    result.push_back(finalClip);
    
    // Remove original
    clipList.removeChild(clipIndex, um);
    
    return result;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ProjectModel.SliceClipAtTime
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/model/ProjectModel.h src/model/ProjectModel.cpp tests/unit/model/clip_slicing_test.cpp
git commit -m "model: add sliceClipAtTimes for clip slicing"
```

---

### Task 7: Transient Detection (Off-Thread)

**Files:**
- Create: `src/engine/TransientDetector.h`, `src/engine/TransientDetector.cpp`
- Modify: `src/engine/StretchRenderer.h/.cpp` (reuse worker thread pattern)
- Test: `tests/unit/engine/transient_detector_test.cpp`

- [ ] **Step 1: Write failing test for transient detection**

```cpp
TEST(TransientDetector, DetectTransientsInSignal)
{
    // Generate test signal: silence + transient + silence
    const int sr = 44100;
    const int len = sr * 2;  // 2 seconds
    juce::AudioBuffer<float> buf(1, len);
    buf.clear();
    
    // Add transient at 0.5s and 1.5s
    buf.setSample(0, sr/2, 1.0f);
    buf.setSample(0, sr/2 + 1, -0.5f);
    buf.setSample(0, sr + sr/2, 0.8f);
    buf.setSample(0, sr + sr/2 + 1, -0.4f);
    
    HDAW::TransientDetector detector;
    auto transients = detector.detect(buf, sr);
    
    EXPECT_GE(transients.size(), 2);
    // First transient near 0.5s
    EXPECT_NEAR(transients[0], 0.5, 0.02);
    // Second near 1.5s
    EXPECT_NEAR(transients[1], 1.5, 0.02);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=TransientDetector.DetectTransientsInSignal
```
Expected: FAIL

- [ ] **Step 3: Implement TransientDetector using spectral flux**

```cpp
// TransientDetector.h
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace HDAW {

class TransientDetector
{
public:
    struct Result {
        std::vector<double> transientTimes;  // in seconds
    };
    
    Result detect(const juce::AudioBuffer<float>& buffer, double sampleRate);
    Result detectFromFile(const juce::String& filePath, juce::AudioFormatManager& fm);
    
private:
    static constexpr int FFT_SIZE = 1024;
    static constexpr int HOP_SIZE = 256;
    
    double spectralFlux(const float* frame, const float* prevFrame, int size) const;
};

} // namespace HDAW
```

```cpp
// TransientDetector.cpp
#include "TransientDetector.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

HDAW::TransientDetector::Result HDAW::TransientDetector::detect(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Result result;
    if (buffer.getNumSamples() < FFT_SIZE) return result;
    
    const float* data = buffer.getReadPointer(0);  // mono mix
    int numFrames = (buffer.getNumSamples() - FFT_SIZE) / HOP_SIZE;
    
    std::vector<float> prevFrame(FFT_SIZE / 2 + 1, 0.0f);
    std::vector<double> fluxValues;
    fluxValues.reserve(numFrames);
    
    juce::dsp::FFT fft(10);  // 2^10 = 1024
    std::vector<float> fftBuffer(FFT_SIZE * 2);
    std::vector<float> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0 * juce::MathConstants<double>::pi * i / (FFT_SIZE - 1)));
    
    for (int frame = 0; frame < numFrames; ++frame)
    {
        int offset = frame * HOP_SIZE;
        
        // Copy and window
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        for (int i = 0; i < FFT_SIZE; ++i)
            fftBuffer[i] = data[offset + i] * window[i];
        
        // FFT
        fft.performRealOnlyForwardTransform(fftBuffer.data());
        
        // Magnitude spectrum
        std::vector<float> mag(FFT_SIZE / 2 + 1);
        for (int i = 0; i <= FFT_SIZE / 2; ++i)
        {
            float re = fftBuffer[i * 2];
            float im = fftBuffer[i * 2 + 1];
            mag[i] = std::sqrt(re * re + im * im);
        }
        
        // Spectral flux
        double flux = 0.0;
        for (int i = 0; i <= FFT_SIZE / 2; ++i)
        {
            double diff = mag[i] - prevFrame[i];
            if (diff > 0) flux += diff;
        }
        fluxValues.push_back(flux);
        prevFrame = mag;
    }
    
    // Peak picking on flux
    double meanFlux = 0.0;
    for (double f : fluxValues) meanFlux += f;
    meanFlux /= fluxValues.size();
    
    double stdFlux = 0.0;
    for (double f : fluxValues) stdFlux += (f - meanFlux) * (f - meanFlux);
    stdFlux = std::sqrt(stdFlux / fluxValues.size());
    
    double threshold = meanFlux + 2.0 * stdFlux;
    double minInterval = 0.05;  // 50ms minimum between transients
    int minFrames = static_cast<int>(minInterval * sampleRate / HOP_SIZE);
    
    int lastPeak = -minFrames;
    for (int i = 1; i < static_cast<int>(fluxValues.size()) - 1; ++i)
    {
        if (fluxValues[i] > fluxValues[i-1] && fluxValues[i] > fluxValues[i+1]
            && fluxValues[i] > threshold
            && i - lastPeak >= minFrames)
        {
            double time = i * HOP_SIZE / sampleRate;
            result.transientTimes.push_back(time);
            lastPeak = i;
        }
    }
    
    return result;
}

HDAW::TransientDetector::Result HDAW::TransientDetector::detectFromFile(const juce::String& filePath, juce::AudioFormatManager& fm)
{
    Result result;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(juce::File(filePath)));
    if (!reader) return result;
    
    juce::AudioBuffer<float> buffer(reader->numChannels, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    
    // Mix to mono
    if (buffer.getNumChannels() > 1)
    {
        juce::AudioBuffer<float> mono(1, buffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            mono.addFrom(0, 0, buffer, ch, 0, buffer.getNumSamples());
        mono.applyGain(1.0f / buffer.getNumChannels());
        return detect(mono, reader->sampleRate);
    }
    return detect(buffer, reader->sampleRate);
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=TransientDetector.DetectTransientsInSignal
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/engine/TransientDetector.h src/engine/TransientDetector.cpp tests/unit/engine/transient_detector_test.cpp
git commit -m "engine: add TransientDetector for slice-at-transients"
```

---

### Task 8: Slice at Transients Command

**Files:**
- Modify: `src/common/ProjectCommands.h/.cpp`, `src/engine/AudioEngineCommands.cpp`
- Test: `tests/integration/commands/slice_commands_test.cpp`

- [ ] **Step 1: Write failing integration test**

```cpp
TEST(SliceCommands, SliceAtTransients)
{
    HDAW::AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    
    // Create clip with known transients (use test file or mock)
    int clipId = 0;
    cmds.createAudioClip("test_transient.wav", 0.0, 4.0, 0, [&](int id) { clipId = id; });
    
    // Slice at transients
    cmds.sliceClipAtTransients(clipId);
    
    // Verify multiple clips created
    auto trackList = model.getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    EXPECT_GT(clipList.getNumChildren(), 1);
    
    // All slices should have same source file
    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        auto c = clipList.getChild(i);
        EXPECT_EQ(c.getProperty(IDs::sourceFile), "test_transient.wav");
        // Offsets should be increasing
        if (i > 0)
        {
            double prevOffset = clipList.getChild(i-1).getProperty(IDs::offset);
            double prevDur = clipList.getChild(i-1).getProperty(IDs::duration);
            double expectedOffset = prevOffset + prevDur;
            EXPECT_DOUBLE_EQ(c.getProperty(IDs::offset), expectedOffset);
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=SliceCommands.SliceAtTransients
```
Expected: FAIL

- [ ] **Step 3: Implement sliceClipAtTransients in AudioEngineCommands**

```cpp
void AudioEngineCommands::sliceClipAtTransients(int clipId)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    
    juce::String sourceFile = clip.getProperty(IDs::sourceFile);
    if (sourceFile.isEmpty()) return;
    
    // Run transient detection off-thread (reuse StretchCache worker pattern)
    auto* pool = &engine.getProjectPool();
    auto* fm = &pool->getFormatManager();
    
    // For now, run synchronously (TODO: async with callback)
    HDAW::TransientDetector detector;
    auto result = detector.detectFromFile(sourceFile, *fm);
    
    if (result.transientTimes.empty()) return;
    
    // Slice at detected transients
    auto slices = HDAW::ProjectModel::sliceClipAtTimes(clip, result.transientTimes, &projectModel.getUndoManager());
    
    // Rebuild routing for new clips
    if (mainProcessor)
        mainProcessor->rebuildRoutingGraph();
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=SliceCommands.SliceAtTransients
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/ProjectCommands.h src/common/ProjectCommands.cpp src/engine/AudioEngineCommands.cpp tests/integration/commands/slice_commands_test.cpp
git commit -m "commands: add sliceClipAtTransients"
```

---

### Task 9: Slice at Playhead Command

**Files:**
- Modify: `src/common/ProjectCommands.h/.cpp`, `src/engine/AudioEngineCommands.cpp`
- Test: `tests/integration/commands/slice_commands_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(SliceCommands, SliceAtPlayhead)
{
    HDAW::AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    
    int clipId = 0;
    cmds.createAudioClip("test.wav", 0.0, 4.0, 0, [&](int id) { clipId = id; });
    
    // Set playhead to 2.0
    cmds.seekToSeconds(2.0);
    
    // Slice at playhead
    cmds.sliceClipAtPlayhead(clipId);
    
    // Verify two clips
    auto clipList = model.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    EXPECT_EQ(clipList.getNumChildren(), 2);
    
    auto c0 = clipList.getChild(0);
    auto c1 = clipList.getChild(1);
    EXPECT_DOUBLE_EQ(c0.getProperty(IDs::duration), 2.0);
    EXPECT_DOUBLE_EQ(c1.getProperty(IDs::duration), 2.0);
    EXPECT_DOUBLE_EQ(c0.getProperty(IDs::offset), 0.0);
    EXPECT_DOUBLE_EQ(c1.getProperty(IDs::offset), 2.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=SliceCommands.SliceAtPlayhead
```
Expected: FAIL

- [ ] **Step 3: Implement sliceClipAtPlayhead**

```cpp
void AudioEngineCommands::sliceClipAtPlayhead(int clipId)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    
    double playhead = transportManager.getCurrentPositionSeconds();
    double clipStart = clip.getProperty(IDs::startTime);
    double clipEnd = clipStart + clip.getProperty(IDs::duration);
    
    if (playhead <= clipStart || playhead >= clipEnd) return;
    
    auto slices = HDAW::ProjectModel::sliceClipAtTimes(clip, {playhead}, &projectModel.getUndoManager());
    if (mainProcessor)
        mainProcessor->rebuildRoutingGraph();
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=SliceCommands.SliceAtPlayhead
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/ProjectCommands.h src/common/ProjectCommands.cpp src/engine/AudioEngineCommands.cpp
git commit -m "commands: add sliceClipAtPlayhead"
```

---

### Task 10: AudioClipEditorWidget - Slicing UI

**Files:**
- Modify: `src/ui/AudioClipEditorWidget.h/.cpp`
- Test: manual verification

- [ ] **Step 1: Add slice buttons to control bar**

```cpp
// In AudioClipEditorWidget.h:
QPushButton* sliceAtPlayheadBtn = nullptr;
QPushButton* sliceAtTransientsBtn = nullptr;
QPushButton* sliceAtSelectionBtn = nullptr;  // if region selected

// In setupUI(), add to control bar:
sliceAtPlayheadBtn = new QPushButton("Slice at Playhead", controlBar);
sliceAtPlayheadBtn->setToolTip("Split clip at current playhead position");
connect(sliceAtPlayheadBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtPlayhead);

sliceAtTransientsBtn = new QPushButton("Slice at Transients", controlBar);
sliceAtTransientsBtn->setToolTip("Auto-slice clip at detected transients");
connect(sliceAtTransientsBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtTransients);

sliceAtSelectionBtn = new QPushButton("Slice at Selection", controlBar);
sliceAtSelectionBtn->setToolTip("Split clip at selected region boundaries");
sliceAtSelectionBtn->setEnabled(false);
connect(sliceAtSelectionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtSelection);
```

```cpp
// In AudioClipEditorWidget.cpp:
void AudioClipEditorWidget::onSliceAtPlayhead()
{
    if (!currentClip.isValid()) return;
    int clipId = currentClip.getProperty(IDs::clipID);
    projectCmds->sliceClipAtPlayhead(clipId);
    // Reload clip list or refresh
    reloadClip();
}

void AudioClipEditorWidget::onSliceAtTransients()
{
    if (!currentClip.isValid()) return;
    int clipId = currentClip.getProperty(IDs::clipID);
    projectCmds->sliceClipAtTransients(clipId);
    reloadClip();
}

void AudioClipEditorWidget::onSliceAtSelection()
{
    if (!currentClip.isValid()) return;
    // Get selection from waveform
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;
    
    int clipId = currentClip.getProperty(IDs::clipID);
    projectCmds->sliceClipAtTimes(clipId, {selStart, selEnd});
    reloadClip();
}
```

- [ ] **Step 2: Add sliceClipAtTimes to ProjectCommands**

```cpp
// ProjectCommands.h:
virtual void sliceClipAtTimes(int clipId, const std::vector<double>& times) = 0;

// AudioEngineCommands.cpp:
void AudioEngineCommands::sliceClipAtTimes(int clipId, const std::vector<double>& times)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    HDAW::ProjectModel::sliceClipAtTimes(clip, times, &projectModel.getUndoManager());
    if (mainProcessor) mainProcessor->rebuildRoutingGraph();
}
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build --config Debug --target HDAW
```
Expected: SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/ui/AudioClipEditorWidget.h src/ui/AudioClipEditorWidget.cpp src/common/ProjectCommands.h src/common/ProjectCommands.cpp src/engine/AudioEngineCommands.cpp
git commit -m "ui: add slice buttons to AudioClipEditorWidget"
```

---

### Task 11: ReadModel - Gain Envelope Access

**Files:**
- Modify: `src/common/ReadModel.h` (declare), `src/engine/ReadModelImpl.cpp` (implement)

- [ ] **Step 1: Add to ReadModel interface**

```cpp
// ReadModel.h:
struct GainEnvelopePoint { double time; double gain; };
virtual std::vector<GainEnvelopePoint> getClipGainEnvelope(int clipId) const = 0;
```

```cpp
// ReadModelImpl.cpp:
std::vector<ReadModel::GainEnvelopePoint> ReadModelImpl::getClipGainEnvelope(int clipId) const
{
    std::vector<GainEnvelopePoint> result;
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return result;
    
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!envelope.isValid()) return result;
    
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        auto pt = envelope.getChild(i);
        result.push_back({ pt.getProperty(IDs::pointTime), pt.getProperty(IDs::pointGain) });
    }
    return result;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --config Debug --target HDAW
```
Expected: SUCCESS

- [ ] **Step 3: Commit**

```bash
git add src/common/ReadModel.h src/engine/ReadModelImpl.cpp
git commit -m "readmodel: add getClipGainEnvelope"
```

---

### Task 12: Full Integration Test & Manual Verification

**Files:**
- Test: `tests/integration/clip_editor_integration_test.cpp`

- [ ] **Step 1: Write integration test covering full workflow**

```cpp
TEST(ClipEditorIntegration, GainEnvelopeAndSlicingWorkflow)
{
    HDAW::AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    
    // 1. Create clip
    int clipId = 0;
    cmds.createAudioClip("test.wav", 0.0, 4.0, 0, [&](int id) { clipId = id; });
    
    // 2. Add gain envelope
    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);
    cmds.addGainEnvelopePoint(clipId, 4.0, 1.0);
    
    // 3. Verify envelope applied to processor
    auto* proc = engine.getMainProcessor()->getClipProcessor(clipId);
    ASSERT_NE(proc, nullptr);
    EXPECT_DOUBLE_EQ(proc->getGainAtTime(1.0), 0.75);
    
    // 4. Slice at playhead
    cmds.seekToSeconds(1.0);
    cmds.sliceClipAtPlayhead(clipId);
    
    // 5. Verify two clips with correct envelopes
    auto clipList = model.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    EXPECT_EQ(clipList.getNumChildren(), 2);
    
    // First slice should have envelope points at 0.0, 1.0 (interpolated)
    auto c0 = clipList.getChild(0);
    auto env0 = c0.getChildWithName(IDs::GAIN_ENVELOPE);
    EXPECT_GE(env0.getNumChildren(), 2);
    
    // 6. Undo slice
    model.getUndoManager().undo();
    EXPECT_EQ(clipList.getNumChildren(), 1);
    
    // 7. Undo envelope
    model.getUndoManager().undo();  // remove point
    model.getUndoManager().undo();  // remove point
    model.getUndoManager().undo();  // remove point
    auto clip = clipList.getChild(0);
    EXPECT_FALSE(clip.getChildWithName(IDs::GAIN_ENVELOPE).isValid());
}
```

- [ ] **Step 2: Run integration test**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ClipEditorIntegration.GainEnvelopeAndSlicingWorkflow
```
Expected: PASS

- [ ] **Step 3: Run full test suite**

```bash
build/Debug/hdaw_tests.exe
```
Expected: ALL 118+ PASS

- [ ] **Step 4: Manual verification checklist**

```
[ ] Launch HDAW Debug build
[ ] Create audio track, import audio file
[ ] Double-click clip → Audio Clip Editor opens
[ ] Gain Envelope tab/section visible
[ ] Click to add points, drag to move, right-click to delete
[ ] Play clip → hear gain changes
[ ] Click "Slice at Playhead" → clip splits, both have envelope
[ ] Click "Slice at Transients" → multiple slices created
[ ] Select region in waveform → "Slice at Selection" enabled
[ ] Undo/Redo works for all operations
[ ] Save project, restart, load → envelopes and slices preserved
```

- [ ] **Step 5: Commit**

```bash
git add tests/integration/clip_editor_integration_test.cpp
git commit -m "test: add clip editor integration test"
```

---

### Task 13: Documentation Update

**Files:**
- Modify: `README.md` (features list)
- Create: `docs/user-guide/clip-editor.md`

- [ ] **Step 1: Update README features**

```markdown
### Audio Clip Editor
- **Gain Envelope**: Draw per-clip volume automation (click to add points, drag to shape, right-click to delete)
- **Slice at Playhead**: Split clip at current playhead position (Ctrl+Shift+S)
- **Slice at Transients**: Auto-detect and slice at transients
- **Slice at Selection**: Split at selected region boundaries
```

- [ ] **Step 2: Create user guide**

```markdown
# Audio Clip Editor Guide

## Gain Envelope
The gain envelope lets you draw volume automation directly on the clip...

## Slicing
Three slicing modes available...
```

- [ ] **Step 3: Commit**

```bash
git add README.md docs/user-guide/clip-editor.md
git commit -m "docs: update README and add clip editor user guide"
```

---

## Self-Review Checklist

- [ ] All spec requirements covered by tasks
- [ ] No placeholder steps - every step has actual code/commands
- [ ] Type consistency: `GainPoint` struct used everywhere, `GAIN_ENVELOPE` IDs match
- [ ] TDD order: test → fail → implement → pass → commit
- [ ] File paths exact and relative to repo root
- [ ] Integration test covers full workflow
- [ ] Manual verification checklist included

---

## Execution Options

**Plan complete and saved to `docs/superpowers/plans/2026-07-15-clip-gain-envelope-and-slicing-plan.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints for review

**Which approach?**