// src/ui/ModulationWidget.cpp
//
// Bottom-panel UI for editing a track's LFO modulation sources. Add/remove
// LFOs and tweak each one's waveform, rate, depth, target, etc. Parameter
// edits are debounced (150ms) and committed per-panel via writeLfoToTree,
// which diffs each control value against the ValueTree before writing — so
// a settled drag doesn't rewrite all 8 properties × N panels every tick.

#include "ModulationWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "../model/ProjectModel.h"
#include <QScrollArea>
#include <cmath>

ModulationWidget::ModulationWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
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

    debounceTimer.setSingleShot(true);
    debounceTimer.setInterval(150);
    connect(&debounceTimer, &QTimer::timeout, this, &ModulationWidget::flushChanges);
}

ModulationWidget::~ModulationWidget()
{
    debounceTimer.stop();
}

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
    // This panel's eventual index in `panels`. Captured by the control lambdas
    // below so they can mark just this panel dirty.
    const int panelIdx = static_cast<int>(panels.size());

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
    // Waveform selection. idClicked fires the integer button id (0–4); route
    // it through the same debounced flush so the new shape reaches the audio
    // side via writeLfoToTree (which reads waveformGroup->checkedId()).
    connect(waveGroup, &QButtonGroup::idClicked, this, [this, panelIdx](int) {
        onLfoParamChanged(panelIdx);
    });

    // Rate
    panel.rateSpin = new QDoubleSpinBox(container);
    panel.rateSpin->setRange(0.01, 100.0);
    panel.rateSpin->setValue(static_cast<double>(modTree.getProperty(IDs::rate, 1.0)));
    panel.rateSpin->setFixedWidth(70);
    panel.rateSpin->setSuffix(" Hz");
    row->addWidget(panel.rateSpin);

    // Sync toggle
    panel.syncBtn = new QPushButton("Sync", container);
    panel.syncBtn->setCheckable(true);
    panel.syncBtn->setChecked(modTree.getProperty(IDs::rateSync, true));
    panel.syncBtn->setFixedHeight(22);
    connect(panel.syncBtn, &QPushButton::toggled, this, [this, panelIdx](bool checked) mutable {
        if (panelIdx < static_cast<int>(panels.size()))
            panels[panelIdx].rateSpin->setSuffix(checked ? "" : " Hz");
        onLfoParamChanged(panelIdx);
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

    connect(panel.depthSlider, &QSlider::valueChanged, this, [this, panelIdx](int v) {
        if (panelIdx < static_cast<int>(panels.size()))
        {
            panels[panelIdx].depthLabel->setText(QString("%1%").arg(v));
        }
        onLfoParamChanged(panelIdx);
    });

    // Bipolar
    panel.bipolarBtn = new QPushButton("Bi", container);
    panel.bipolarBtn->setCheckable(true);
    panel.bipolarBtn->setChecked(modTree.getProperty(IDs::bipolar, false));
    panel.bipolarBtn->setFixedSize(28, 22);
    panel.bipolarBtn->setToolTip("Bipolar (±1) / Unipolar (0→1)");
    connect(panel.bipolarBtn, &QPushButton::toggled, this, [this, panelIdx](bool) {
        onLfoParamChanged(panelIdx);
    });
    row->addWidget(panel.bipolarBtn);

    // Phase offset
    panel.phaseSpin = new QDoubleSpinBox(container);
    panel.phaseSpin->setRange(0.0, 360.0);
    panel.phaseSpin->setValue(static_cast<double>(modTree.getProperty(IDs::phaseOffset, 0.0)));
    panel.phaseSpin->setSuffix("\xC2\xB0");
    panel.phaseSpin->setFixedWidth(60);
    panel.phaseSpin->setToolTip("Phase offset");
    connect(panel.phaseSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, panelIdx](double) { onLfoParamChanged(panelIdx); });
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
            this, [this, panelIdx](int) { onLfoParamChanged(panelIdx); });
    row->addWidget(panel.targetCombo);

    // Bypass
    panel.bypassBtn = new QPushButton("Byp", container);
    panel.bypassBtn->setCheckable(true);
    panel.bypassBtn->setChecked(!static_cast<bool>(modTree.getProperty(IDs::enabled, true)));
    panel.bypassBtn->setFixedSize(32, 22);
    connect(panel.bypassBtn, &QPushButton::toggled, this, [this, panelIdx](bool) {
        onLfoParamChanged(panelIdx);
    });
    row->addWidget(panel.bypassBtn);

    // Remove
    panel.removeBtn = new QPushButton("\xC3\x97", container);
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

    projectCmds->beginTransaction("Add LFO");

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
    newMod.setProperty(IDs::enabled, true, nullptr);
    modList.addChild(newMod, -1, &model.getUndoManager());

    projectCmds->endTransaction();

    rebuildPanels();
    syncModulationToAudio();
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

    projectCmds->beginTransaction("Remove LFO");
    modList.removeChild(modList.getChild(lfoIndex), &model.getUndoManager());
    projectCmds->endTransaction();

    rebuildPanels();
    syncModulationToAudio();
}

bool ModulationWidget::writeLfoToTree(int lfoIndex)
{
    if (currentTrack < 0 || lfoIndex >= static_cast<int>(panels.size())) return false;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return false;

    auto trackTree = trackList.getChild(currentTrack);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex >= modList.getNumChildren()) return false;

    auto modTree = modList.getChild(lfoIndex);
    auto& p = panels[lfoIndex];
    auto& um = model.getUndoManager();

    // Diff each control value against the tree's current value and only write
    // what actually changed. Each setProperty would otherwise register a
    // separate undoable action and (via the AudioEngine MODULATION listener)
    // trigger a rebuildModulation — so a single slider drag settled into a
    // no-op flush and still rewrote all 8 props × N panels every 150ms.
    bool wrote = false;

    int waveId = p.waveformGroup->checkedId();
    if (waveId >= 0 && waveId != static_cast<int>(modTree.getProperty(IDs::waveform, 0)))
    {
        modTree.setProperty(IDs::waveform, waveId, &um);
        wrote = true;
    }
    double rate = p.rateSpin->value();
    if (rate != static_cast<double>(modTree.getProperty(IDs::rate, 1.0)))
    {
        modTree.setProperty(IDs::rate, rate, &um);
        wrote = true;
    }
    bool rateSync = p.syncBtn->isChecked();
    if (rateSync != static_cast<bool>(modTree.getProperty(IDs::rateSync, true)))
    {
        modTree.setProperty(IDs::rateSync, rateSync, &um);
        wrote = true;
    }
    double depth = p.depthSlider->value() / 100.0;
    if (std::abs(depth - static_cast<double>(modTree.getProperty(IDs::depth, 0.0))) > 1e-9)
    {
        modTree.setProperty(IDs::depth, depth, &um);
        wrote = true;
    }
    bool bipolar = p.bipolarBtn->isChecked();
    if (bipolar != static_cast<bool>(modTree.getProperty(IDs::bipolar, false)))
    {
        modTree.setProperty(IDs::bipolar, bipolar, &um);
        wrote = true;
    }
    double phase = p.phaseSpin->value();
    if (std::abs(phase - static_cast<double>(modTree.getProperty(IDs::phaseOffset, 0.0))) > 1e-9)
    {
        modTree.setProperty(IDs::phaseOffset, phase, &um);
        wrote = true;
    }
    int target = p.targetCombo->currentData().toInt();
    if (target != static_cast<int>(modTree.getProperty(IDs::targetParamID, 1)))
    {
        modTree.setProperty(IDs::targetParamID, target, &um);
        wrote = true;
    }
    bool enabled = !p.bypassBtn->isChecked();
    if (enabled != static_cast<bool>(modTree.getProperty(IDs::enabled, true)))
    {
        modTree.setProperty(IDs::enabled, enabled, &um);
        wrote = true;
    }

    return wrote;
}

void ModulationWidget::onLfoParamChanged(int lfoIndex)
{
    if (lfoIndex >= 0 && lfoIndex < static_cast<int>(panels.size()))
        panels[lfoIndex].dirty = true;
    debounceTimer.start();
}

void ModulationWidget::flushChanges()
{
    // Only commit panels that actually changed since the last flush. This
    // avoids rewriting every property on every panel on each 150ms tick —
    // each setProperty is an undoable action and triggers an audio-side
    // rebuildModulation via the AudioEngine listener.
    bool anyWrote = false;
    for (int i = 0; i < static_cast<int>(panels.size()); ++i)
    {
        if (!panels[i].dirty) continue;
        if (writeLfoToTree(i))
            anyWrote = true;
        panels[i].dirty = false;
    }
    if (anyWrote)
        syncModulationToAudio();
}

void ModulationWidget::syncModulationToAudio()
{
    if (currentTrack < 0) return;
    if (auto* mainProc = engine.getMainProcessor())
        mainProc->rebuildModulation(currentTrack);
}
