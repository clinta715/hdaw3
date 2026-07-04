# Per-Track LFO Modulation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-track LFO modulation with a new Modulation tab in the bottom panel.

**Architecture:** Each Track gets a `ModulationManager` containing `LFOModulationSource` objects. LFO parameters are `std::atomic<T>` so the audio thread reads them lock-free in the per-sample loop. A new `ModulationWidget` bottom-panel tab lets users add/configure LFOs per track.

**Tech Stack:** Qt 6 (UI), JUCE 8 (engine/audio), ValueTree (persistence)

---

### Task 1: Add modulation IDs to ProjectModel.h

**Files:**
- Modify: `src/model/ProjectModel.h`

- [ ] **Step 1: Add modulation tree IDs after `DECLARE_ID(SCALE_INFO)`**

```cpp
    // Modulation
    DECLARE_ID(MODULATION_LIST)
    DECLARE_ID(MODULATION)
    DECLARE_ID(waveform)
    DECLARE_ID(rate)
    DECLARE_ID(rateSync)
    DECLARE_ID(depth)
    DECLARE_ID(bipolar)
    DECLARE_ID(phaseOffset)
    DECLARE_ID(targetParamID)
    DECLARE_ID(targetClipIndex)
```

Place them right before `#undef DECLARE_ID` (line 129).

- [ ] **Step 2: Commit**

```bash
git add src/model/ProjectModel.h
git commit -m "engine: add modulation IDs to ProjectModel"
```

---

### Task 2: Create ModulationSource.h

**Files:**
- Create: `src/engine/ModulationSource.h`

`LFOModulationSource` implements a single LFO with 5 waveforms. All
config is in atomics so the audio thread reads them without locks.

- [ ] **Step 1: Write the file**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <atomic>
#include <random>

namespace HDAW {

// Base class for future modulation source types (envelope follower, step seq, etc.)
class ModulationSource {
public:
    virtual ~ModulationSource() = default;
    virtual void prepare(double sampleRate) = 0;
    virtual float processSample(double phase) = 0;
};

enum class LfoWaveform : int {
    sine = 0,
    triangle,
    saw,
    square,
    sampleAndHold
};

class LFOModulationSource : public ModulationSource {
public:
    LFOModulationSource();

    void prepare(double sr) override;
    float processSample(double phase) override;

    // Called from the per-sample loop. Returns the modulation offset
    // for this sample. Handles phase accumulation internally.
    float getNextValue(double bpm, double sampleRate);

    // Called from the audio thread — reads atomics
    float getDepth() const noexcept { return depth.load(std::memory_order_relaxed); }
    float getRate() const noexcept { return rate.load(std::memory_order_relaxed); }
    bool  isRateSynced() const noexcept { return rateSync.load(std::memory_order_relaxed); }
    LfoWaveform getWaveform() const noexcept { return static_cast<LfoWaveform>(waveform.load(std::memory_order_relaxed)); }
    bool  isBipolar() const noexcept { return bipolar.load(std::memory_order_relaxed); }
    float getPhaseOffset() const noexcept { return phaseOffset.load(std::memory_order_relaxed); }
    int   getTargetParamID() const noexcept { return targetParamID.load(std::memory_order_relaxed); }
    bool  isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

    // Called from the UI thread — writes atomics
    void setDepth(float v) noexcept { depth.store(v, std::memory_order_relaxed); }
    void setRate(float v) noexcept { rate.store(v, std::memory_order_relaxed); }
    void setRateSync(bool v) noexcept { rateSync.store(v, std::memory_order_relaxed); }
    void setWaveform(LfoWaveform w) noexcept { waveform.store(static_cast<int>(w), std::memory_order_relaxed); }
    void setBipolar(bool v) noexcept { bipolar.store(v, std::memory_order_relaxed); }
    void setPhaseOffset(float v) noexcept { phaseOffset.store(v, std::memory_order_relaxed); }
    void setTargetParamID(int v) noexcept { targetParamID.store(v, std::memory_order_relaxed); }
    void setEnabled(bool v) noexcept { enabled.store(v, std::memory_order_relaxed); }

    // Sync state from a ValueTree MODULATION node
    void fromValueTree(const juce::ValueTree& tree);
    juce::ValueTree toValueTree(const juce::String& id) const;

private:
    float lookupWaveform(double normPhase) const;

    // Atomics — written by UI thread, read by audio thread
    std::atomic<float>  depth{0.0f};
    std::atomic<float>  rate{1.0f};
    std::atomic<int>    waveform{0};
    std::atomic<bool>   rateSync{true};
    std::atomic<bool>   bipolar{false};
    std::atomic<float>  phaseOffset{0.0f};
    std::atomic<int>    targetParamID{1};
    std::atomic<bool>   enabled{true};

    // Audio-thread only state
    double currentPhase = 0.0;
    double sampleRate = 44100.0;

    // S&H state
    double lastShValue = 0.0;
    double shPhase = 0.0; // tracks when to generate new S&H value
};

// ── inline implementations ──

inline LFOModulationSource::LFOModulationSource()
{
    std::mt19937 rng{42};
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    lastShValue = dist(rng);
}

inline void LFOModulationSource::prepare(double sr)
{
    sampleRate = sr;
    currentPhase = 0.0;
    shPhase = 0.0;
}

inline float LFOModulationSource::processSample(double) { return 0.0f; } // unused, use getNextValue

inline float LFOModulationSource::getNextValue(double bpm, double sr)
{
    if (!enabled.load(std::memory_order_relaxed))
        return 0.0f;

    double phaseStep;
    if (rateSync.load(std::memory_order_relaxed))
    {
        // rate = cycles per beat. 1.0 = quarter note, 4.0 = sixteenth.
        double freq = rate.load(std::memory_order_relaxed) * (bpm / 60.0);
        phaseStep = freq / sr;
    }
    else
    {
        phaseStep = rate.load(std::memory_order_relaxed) / sr;
    }

    currentPhase += phaseStep;
    if (currentPhase >= 1.0)
        currentPhase -= std::floor(currentPhase);

    float phaseOff = phaseOffset.load(std::memory_order_relaxed);
    double normPhase = std::fmod(currentPhase + phaseOff / 360.0, 1.0);
    if (normPhase < 0.0) normPhase += 1.0;

    float value = lookupWaveform(normPhase);
    float d = depth.load(std::memory_order_relaxed);

    if (!bipolar.load(std::memory_order_relaxed))
        value = value * 0.5f + 0.5f; // map [-1,1] to [0,1]

    return value * d;
}

inline float LFOModulationSource::lookupWaveform(double p) const
{
    switch (waveform.load(std::memory_order_relaxed))
    {
        case 0: // sine
            return static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * p));

        case 1: // triangle
            return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);

        case 2: // saw
            return static_cast<float>(2.0 * p - 1.0);

        case 3: // square
            return p < 0.5f ? 1.0f : -1.0f;

        case 4: // sample & hold
        {
            // Update S&H value once per cycle
            if (p < shPhase)
            {
                std::mt19937 rng{static_cast<unsigned int>(p * 1000000.0)};
                std::uniform_real_distribution<double> dist(-1.0, 1.0);
                lastShValue = dist(rng);
            }
            shPhase = p;
            return static_cast<float>(lastShValue);
        }

        default:
            return 0.0f;
    }
}

inline void LFOModulationSource::fromValueTree(const juce::ValueTree& tree)
{
    setWaveform(static_cast<LfoWaveform>(static_cast<int>(tree.getProperty(IDs::waveform, 0))));
    setRate(static_cast<float>(static_cast<double>(tree.getProperty(IDs::rate, 1.0))));
    setRateSync(tree.getProperty(IDs::rateSync, true));
    setDepth(static_cast<float>(static_cast<double>(tree.getProperty(IDs::depth, 0.0))));
    setBipolar(tree.getProperty(IDs::bipolar, false));
    setPhaseOffset(static_cast<float>(static_cast<double>(tree.getProperty(IDs::phaseOffset, 0.0))));
    setTargetParamID(tree.getProperty(IDs::targetParamID, 1));
    setEnabled(tree.getProperty(IDs::enabled, true));
}

inline juce::ValueTree LFOModulationSource::toValueTree(const juce::String& id) const
{
    auto tree = juce::ValueTree(IDs::MODULATION);
    tree.setProperty(IDs::name, "LFO", nullptr);
    tree.setProperty("id", id, nullptr);
    tree.setProperty("type", "lfo", nullptr);
    tree.setProperty(IDs::waveform, static_cast<int>(getWaveform()), nullptr);
    tree.setProperty(IDs::rate, static_cast<double>(getRate()), nullptr);
    tree.setProperty(IDs::rateSync, isRateSynced(), nullptr);
    tree.setProperty(IDs::depth, static_cast<double>(getDepth()), nullptr);
    tree.setProperty(IDs::bipolar, isBipolar(), nullptr);
    tree.setProperty(IDs::phaseOffset, static_cast<double>(getPhaseOffset()), nullptr);
    tree.setProperty(IDs::targetParamID, getTargetParamID(), nullptr);
    tree.setProperty(IDs::targetClipIndex, -1, nullptr);
    tree.setProperty(IDs::enabled, isEnabled(), nullptr);
    return tree;
}

} // namespace HDAW
```

- [ ] **Step 2: Commit**

```bash
git add src/engine/ModulationSource.h
git commit -m "engine: add LFOModulationSource with 5 waveforms"
```

---

### Task 3: Create ModulationManager.h

**Files:**
- Create: `src/engine/ModulationManager.h`

Per-track container that holds a vector of modulation sources. The audio
thread calls `getModulation(paramID, bpm, sampleRate)` per sample; the
UI thread calls `rebuild(tree)` to sync from ValueTree.

- [ ] **Step 1: Write the file**

```cpp
#pragma once
#include "ModulationSource.h"
#include "../model/ProjectModel.h"
#include <vector>
#include <memory>
#include <atomic>

namespace HDAW {

class ModulationManager {
public:
    ModulationManager() = default;

    void prepare(double sampleRate);

    // Rebuild the source list from the track's MODULATION_LIST ValueTree.
    // Called on the UI thread under stateLock.
    void rebuild(const juce::ValueTree& modulationListTree, double sampleRate);

    // Called per-sample from the audio thread.
    // Returns the sum of all enabled modulation source outputs targeting paramID.
    float getModulation(int paramID, double bpm, double sampleRate);

    int getNumSources() const { return static_cast<int>(sources.size()); }
    LFOModulationSource* getSource(int index);

private:
    std::vector<std::unique_ptr<LFOModulationSource>> sources;
    double sampleRate = 44100.0;
};

// ── inline implementations ──

inline void ModulationManager::prepare(double sr)
{
    sampleRate = sr;
    for (auto& s : sources)
        if (s) s->prepare(sr);
}

inline void ModulationManager::rebuild(const juce::ValueTree& modListTree, double sr)
{
    sampleRate = sr;
    sources.clear();
    if (!modListTree.isValid()) return;

    for (int i = 0; i < modListTree.getNumChildren(); ++i)
    {
        auto modTree = modListTree.getChild(i);
        juce::String type = modTree.getProperty("type", "lfo").toString();
        if (type != "lfo") continue;

        auto src = std::make_unique<LFOModulationSource>();
        src->fromValueTree(modTree);
        src->prepare(sr);
        sources.push_back(std::move(src));
    }
}

inline float ModulationManager::getModulation(int paramID, double bpm, double sr)
{
    float sum = 0.0f;
    for (auto& s : sources)
    {
        if (!s || !s->isEnabled()) continue;
        if (s->getTargetParamID() != paramID) continue;
        sum += s->getNextValue(bpm, sr);
    }
    return sum;
}

inline LFOModulationSource* ModulationManager::getSource(int index)
{
    if (index < 0 || index >= static_cast<int>(sources.size()))
        return nullptr;
    return sources[index].get();
}

} // namespace HDAW
```

- [ ] **Step 2: Commit**

```bash
git add src/engine/ModulationManager.h
git commit -m "engine: add ModulationManager (per-track container)"
```

---

### Task 4: Wire modulation into Track.h/.cpp

**Files:**
- Modify: `src/engine/Track.h`
- Modify: `src/engine/Track.cpp`

Add the `ModulationManager` member, the `rebuildModulation()` method,
and the per-sample modulation evaluation in `processBlock`.

- [ ] **Step 1: Add include and member to Track.h**

Add `#include "ModulationManager.h"` after `#include "AutomationManager.h"` (line 7).

Add after `std::vector<std::unique_ptr<AutomationManager>> automationManagers;` (line 90):

```cpp
    std::unique_ptr<ModulationManager> modulationManager;
```

Add a public method declaration after `setPluginManager` (line 49):

```cpp
    void rebuildModulation(const juce::ValueTree& modulationListTree);
```

- [ ] **Step 2: Initialize in Track constructor**

Add to Track constructor in `Track.cpp` (after line 11):

```cpp
    modulationManager = std::make_unique<ModulationManager>();
```

- [ ] **Step 3: Prepare in prepareToPlay**

Add after the automation loop in `Track::prepareToPlay` (after line 31):

```cpp
    if (modulationManager)
        modulationManager->prepare(sampleRate);
```

- [ ] **Step 4: Implement rebuildModulation**

Add after `rebuildFXChain` (after line 186):

```cpp
void Track::rebuildModulation(const juce::ValueTree& modulationListTree)
{
    if (!modulationManager) return;
    modulationManager->rebuild(modulationListTree, getSampleRate());
}
```

- [ ] **Step 5: Add modulation to the per-sample loop**

Replace the per-sample loop in `processBlock` (lines 242-252) with:

```cpp
        // Get BPM from playhead for beat-synced modulation
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                bpm = pos->getBpm().orFallback(120.0);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float baseGain = volumeGain.getNextValue();
            float basePan  = panPosition.getNextValue();

            float modGain = 0.0f, modPan = 0.0f;
            if (modulationManager)
            {
                modGain = modulationManager->getModulation(1, bpm, getSampleRate());
                modPan  = modulationManager->getModulation(2, bpm, getSampleRate());
            }

            float currentGain = std::clamp(baseGain + modGain, 0.0f, 1.0f);
            float currentPan  = std::clamp(basePan  + modPan,  -1.0f, 1.0f);

            float leftGain = currentGain * (1.0f - currentPan);
            float rightGain = currentGain * currentPan;

            leftChannel[sample] *= leftGain;
            rightChannel[sample] *= rightGain;
        }
```

- [ ] **Step 6: Commit**

```bash
git add src/engine/Track.h src/engine/Track.cpp
git commit -m "engine: wire modulation into Track (per-sample LFO eval)"
```

---

### Task 5: Call rebuildModulation from RoutingManager

**Files:**
- Modify: `src/engine/RoutingManager.cpp`

When rebuilding a track's FX chain, also rebuild its modulation state
from the ValueTree.

- [ ] **Step 1: Add modulation rebuild to rebuildTrackFX**

Add after `trackIt->second->rebuildFXChain(fxChainTree);` in `RoutingManager::rebuildTrackFX` (line 419):

```cpp
    auto modulationListTree = trackTree.getChildWithName(IDs::MODULATION_LIST);
    trackIt->second->rebuildModulation(modulationListTree);
```

- [ ] **Step 2: Commit**

```bash
git add src/engine/RoutingManager.cpp
git commit -m "engine: rebuild modulation state in RoutingManager::rebuildTrackFX"
```

---

### Task 6: Create ModulationWidget (UI)

**Files:**
- Create: `src/ui/ModulationWidget.h`
- Create: `src/ui/ModulationWidget.cpp`

Bottom-panel tab that shows LFOs for the selected track. Each LFO is a
row with waveform buttons, rate, depth, target, bypass, and remove.

- [ ] **Step 1: Write ModulationWidget.h**

```cpp
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QScrollArea>
#include "../engine/AudioEngine.h"
#include "../engine/ModulationManager.h"
#include <vector>

class ModulationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ModulationWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~ModulationWidget() override;

public slots:
    void loadTrack(int trackIndex);

private slots:
    void onAddLFO();
    void onRemoveLFO(int lfoIndex);
    void onLfoParamChanged();

private:
    struct LfoPanel {
        int lfoIndex;
        QWidget* container;
        QButtonGroup* waveformGroup;
        QDoubleSpinBox* rateSpin;
        QPushButton* syncBtn;
        QSlider* depthSlider;
        QLabel* depthLabel;
        QPushButton* bipolarBtn;
        QDoubleSpinBox* phaseSpin;
        QComboBox* targetCombo;
        QPushButton* bypassBtn;
        QPushButton* removeBtn;
    };

    void clearPanels();
    void rebuildPanels();
    int addPanel(const juce::ValueTree& modTree, int index);
    void writeLfoToTree(int lfoIndex);

    AudioEngine& engine;
    int currentTrack = -1;
    QVBoxLayout* listLayout;
    QWidget* listWidget;
    QPushButton* addBtn;
    QLabel* trackLabel;
    std::vector<LfoPanel> panels;
};
```

- [ ] **Step 2: Write ModulationWidget.cpp**

```cpp
#include "ModulationWidget.h"
#include "Theme.h"
#include "../model/ProjectModel.h"
#include <QScrollArea>

ModulationWidget::ModulationWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Top bar
    auto* topBar = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 4, 8, 4);

    trackLabel = new QLabel("No track selected", topBar);
    topLayout->addWidget(trackLabel);

    topLayout->addStretch();

    addBtn = new QPushButton("+ Add LFO", topBar);
    connect(addBtn, &QPushButton::clicked, this, &ModulationWidget::onAddLFO);
    topLayout->addWidget(addBtn);

    outerLayout->addWidget(topBar);

    // Scrollable LFO list
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    listWidget = new QWidget(scrollArea);
    listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(8, 4, 8, 4);
    listLayout->setSpacing(4);
    listLayout->addStretch();

    scrollArea->setWidget(listWidget);
    outerLayout->addWidget(scrollArea);
}

ModulationWidget::~ModulationWidget() = default;

void ModulationWidget::loadTrack(int trackIndex)
{
    currentTrack = trackIndex;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();

    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        trackLabel->setText("No track selected");
        clearPanels();
        return;
    }

    auto trackTree = trackList.getChild(trackIndex);
    juce::String trackName = trackTree.getProperty(IDs::name).toString();
    trackLabel->setText(QString("Track: %1").arg(QString::fromUtf8(trackName.toRawUTF8())));

    rebuildPanels();
}

void ModulationWidget::clearPanels()
{
    for (auto& p : panels)
    {
        listLayout->removeWidget(p.container);
        p.container->deleteLater();
    }
    panels.clear();
}

void ModulationWidget::rebuildPanels()
{
    clearPanels();

    if (currentTrack < 0) return;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(currentTrack);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid()) return;

    for (int i = 0; i < modList.getNumChildren(); ++i)
        addPanel(modList.getChild(i), i);
}

int ModulationWidget::addPanel(const juce::ValueTree& modTree, int index)
{
    auto* container = new QWidget(listWidget);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(6);

    // Insert before the stretch
    if (auto* stretch = listLayout->itemAt(listLayout->count() - 1))
        listLayout->insertWidget(listLayout->count() - 1, container);
    else
        listLayout->addWidget(container);

    LfoPanel panel;
    panel.lfoIndex = index;
    panel.container = container;

    // Waveform buttons
    auto* waveGroup = new QButtonGroup(container);
    panel.waveformGroup = waveGroup;
    QStringList waveLabels = {"Sin", "Tri", "Saw", "Sqr", "S&H"};
    int currentWave = modTree.getProperty(IDs::waveform, 0);
    for (int w = 0; w < 5; ++w)
    {
        auto* btn = new QPushButton(waveLabels[w], container);
        btn->setFixedSize(32, 22);
        btn->setCheckable(true);
        btn->setStyleSheet("font-size: 7pt;");
        waveGroup->addButton(btn, w);
        if (w == currentWave) btn->setChecked(true);
        row->addWidget(btn);
    }
    connect(waveGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int) { onLfoParamChanged(); });

    // Rate
    panel.rateSpin = new QDoubleSpinBox(container);
    panel.rateSpin->setRange(0.01, 100.0);
    panel.rateSpin->setValue(static_cast<double>(modTree.getProperty(IDs::rate, 1.0)));
    panel.rateSpin->setFixedWidth(70);
    panel.rateSpin->setSuffix(" Hz");
    row->addWidget(panel.rateSpin);
    connect(panel.rateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onLfoParamChanged(); });

    // Sync toggle
    panel.syncBtn = new QPushButton("Sync", container);
    panel.syncBtn->setCheckable(true);
    panel.syncBtn->setChecked(modTree.getProperty(IDs::rateSync, true));
    panel.syncBtn->setFixedHeight(22);
    connect(panel.syncBtn, &QPushButton::toggled, this, [this, panel](bool checked) mutable {
        panel.rateSpin->setSuffix(checked ? "" : " Hz");
        onLfoParamChanged();
    });
    row->addWidget(panel.syncBtn);
    if (panel.syncBtn->isChecked())
        panel.rateSpin->setSuffix("");

    // Depth slider
    panel.depthSlider = new QSlider(Qt::Horizontal, container);
    panel.depthSlider->setRange(0, 100);
    panel.depthSlider->setValue(static_cast<int>(static_cast<double>(modTree.getProperty(IDs::depth, 0.0)) * 100.0));
    panel.depthSlider->setFixedWidth(80);
    row->addWidget(panel.depthSlider);

    panel.depthLabel = new QLabel(QString("%1%").arg(panel.depthSlider->value()), container);
    panel.depthLabel->setFixedWidth(30);
    row->addWidget(panel.depthLabel);
    connect(panel.depthSlider, &QSlider::valueChanged, this, [this, &panel](int v) {
        panel.depthLabel->setText(QString("%1%").arg(v));
        onLfoParamChanged();
    });

    // Bipolar
    panel.bipolarBtn = new QPushButton("Bi", container);
    panel.bipolarBtn->setCheckable(true);
    panel.bipolarBtn->setChecked(modTree.getProperty(IDs::bipolar, false));
    panel.bipolarBtn->setFixedSize(28, 22);
    panel.bipolarBtn->setToolTip("Bipolar (±1) / Unipolar (0→1)");
    connect(panel.bipolarBtn, &QPushButton::toggled, this, [this](bool) { onLfoParamChanged(); });
    row->addWidget(panel.bipolarBtn);

    // Phase offset
    panel.phaseSpin = new QDoubleSpinBox(container);
    panel.phaseSpin->setRange(0.0, 360.0);
    panel.phaseSpin->setValue(static_cast<double>(modTree.getProperty(IDs::phaseOffset, 0.0)));
    panel.phaseSpin->setSuffix("\xC2\xB0"); // degree symbol
    panel.phaseSpin->setFixedWidth(60);
    panel.phaseSpin->setToolTip("Phase offset");
    connect(panel.phaseSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onLfoParamChanged(); });
    row->addWidget(panel.phaseSpin);

    // Target parameter
    panel.targetCombo = new QComboBox(container);
    panel.targetCombo->addItem("Volume", 1);
    panel.targetCombo->addItem("Pan", 2);
    int currentTarget = modTree.getProperty(IDs::targetParamID, 1);
    int comboIdx = panel.targetCombo->findData(currentTarget);
    if (comboIdx >= 0) panel.targetCombo->setCurrentIndex(comboIdx);
    panel.targetCombo->setFixedWidth(90);
    connect(panel.targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onLfoParamChanged(); });
    row->addWidget(panel.targetCombo);

    // Bypass
    panel.bypassBtn = new QPushButton("Byp", container);
    panel.bypassBtn->setCheckable(true);
    panel.bypassBtn->setChecked(!static_cast<bool>(modTree.getProperty(IDs::enabled, true)));
    panel.bypassBtn->setFixedSize(32, 22);
    connect(panel.bypassBtn, &QPushButton::toggled, this, [this](bool) { onLfoParamChanged(); });
    row->addWidget(panel.bypassBtn);

    // Remove
    panel.removeBtn = new QPushButton("\xC3\x97", container); // ×
    panel.removeBtn->setFixedSize(22, 22);
    connect(panel.removeBtn, &QPushButton::clicked, this, [this, index]() {
        onRemoveLFO(index);
    });
    row->addWidget(panel.removeBtn);

    panels.push_back(std::move(panel));
    return panels.size() - 1;
}

void ModulationWidget::onAddLFO()
{
    if (currentTrack < 0) return;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(currentTrack);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid())
    {
        modList = juce::ValueTree(IDs::MODULATION_LIST);
        trackTree.addChild(modList, -1, &model.getUndoManager());
    }

    auto newMod = juce::ValueTree(IDs::MODULATION);
    newMod.setProperty("id", juce::String("lfo_") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty("type", "lfo", nullptr);
    newMod.setProperty(IDs::name, juce::String("LFO ") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty(IDs::waveform, 0, nullptr);
    newMod.setProperty(IDs::rate, 1.0, nullptr);
    newMod.setProperty(IDs::rateSync, true, nullptr);
    newMod.setProperty(IDs::depth, 0.3, nullptr);
    newMod.setProperty(IDs::bipolar, false, nullptr);
    newMod.setProperty(IDs::phaseOffset, 0.0, nullptr);
    newMod.setProperty(IDs::targetParamID, 1, nullptr);
    newMod.setProperty(IDs::targetClipIndex, -1, nullptr);
    newMod.setProperty(IDs::enabled, true, nullptr);
    modList.addChild(newMod, -1, &model.getUndoManager());

    rebuildPanels();
}

void ModulationWidget::onRemoveLFO(int lfoIndex)
{
    if (currentTrack < 0) return;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(currentTrack);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex >= modList.getNumChildren()) return;

    modList.removeChild(modList.getChild(lfoIndex), &model.getUndoManager());
    rebuildPanels();
}

void ModulationWidget::writeLfoToTree(int lfoIndex)
{
    if (currentTrack < 0 || lfoIndex >= static_cast<int>(panels.size())) return;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(currentTrack);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex >= modList.getNumChildren()) return;

    auto modTree = modList.getChild(lfoIndex);
    auto& p = panels[lfoIndex];

    int waveId = p.waveformGroup->checkedId();
    if (waveId >= 0) modTree.setProperty(IDs::waveform, waveId, &model.getUndoManager());
    modTree.setProperty(IDs::rate, p.rateSpin->value(), &model.getUndoManager());
    modTree.setProperty(IDs::rateSync, p.syncBtn->isChecked(), &model.getUndoManager());
    modTree.setProperty(IDs::depth, p.depthSlider->value() / 100.0, &model.getUndoManager());
    modTree.setProperty(IDs::bipolar, p.bipolarBtn->isChecked(), &model.getUndoManager());
    modTree.setProperty(IDs::phaseOffset, p.phaseSpin->value(), &model.getUndoManager());
    modTree.setProperty(IDs::targetParamID, p.targetCombo->currentData().toInt(), &model.getUndoManager());
    modTree.setProperty(IDs::enabled, !p.bypassBtn->isChecked(), &model.getUndoManager());
}

void ModulationWidget::onLfoParamChanged()
{
    // Find which LFO panel triggered the change by comparing sender()
    auto* senderWidget = qobject_cast<QWidget*>(sender());
    if (!senderWidget) return;

    for (int i = 0; i < static_cast<int>(panels.size()); ++i)
    {
        if (panels[i].container == senderWidget ||
            panels[i].container->isAncestorOf(senderWidget))
        {
            writeLfoToTree(i);
            return;
        }
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/ModulationWidget.h src/ui/ModulationWidget.cpp
git commit -m "ui: add ModulationWidget (LFO editor panel)"
```

---

### Task 7: Wire ModulationWidget into MainWindow

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

Add the Modulation tab to the bottom panel stack and wire track
selection to load LFOs.

- [ ] **Step 1: Forward-declare and add member to MainWindow.h**

Add `class ModulationWidget;` forward declaration in the class-level
forward declarations area (after line 25 or so).

Add member after `StepEditorWidget* stepEditorWidget;` (around line 107):

```cpp
    ModulationWidget* modulationWidget = nullptr;
```

- [ ] **Step 2: Create tab and stack widget entry in MainWindow.cpp**

In `setupBottomPanel()`, after the step tab line (line 420), add:

```cpp
    auto* modTab = makeTab("Modulation", 6);
    juce::ignoreUnused(modTab);
```

After `bottomStack->addWidget(stepEditorWidget);` (line 444), add:

```cpp
    modulationWidget = new ModulationWidget(engine, bottomStack);
    bottomStack->addWidget(modulationWidget);
```

- [ ] **Step 3: Connect track selection signal**

In `connectTimelineSignals()` or at the end of `setupBottomPanel()`, add:

```cpp
    connect(timelineView, &TimelineView::trackSelected,
            this, [this](int trackIdx) {
                if (modulationWidget)
                    modulationWidget->loadTrack(trackIdx);
            });
```

Also wire the tab click to load the modulation widget:

Add to the `tabGroup->idClicked` lambda (after line 451):

```cpp
        if (id == 6 && selectedTrack >= 0)
            modulationWidget->loadTrack(selectedTrack);
```

- [ ] **Step 4: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "ui: add Modulation tab to bottom panel"
```

---

### Task 8: Add new source files to CMakeLists.txt

**Files:**
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Add ModulationWidget sources**

Find the `add_executable(HDAW_lib` block and add after the `AutomationLaneWidget` entries:

```cmake
    ui/ModulationWidget.h
    ui/ModulationWidget.cpp
```

Also add the new engine headers (ModulationSource.h, ModulationManager.h)
are already known because they're included from Track.h, which is in the
same target. They don't need their own compilation entries since they're
header-only.

- [ ] **Step 2: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: add ModulationWidget sources to CMake"
```

---

### Task 9: Build and smoke test

- [ ] **Step 1: Build HDAW_lib**

```bash
cmake --build build --config Debug --target HDAW_lib 2>&1 | tail -20
```

Fix any compile errors.

- [ ] **Step 2: Build HDAW executable**

```bash
cmake --build build --config Debug --target HDAW 2>&1 | tail -20
```

Fix any link errors.

- [ ] **Step 3: Commit any build fixes**

```bash
git commit -am "fix: address build issues from modulation integration"
```
