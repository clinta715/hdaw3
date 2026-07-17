// src/ui/ModulationWidget.cpp
//
// Bottom-panel UI for editing a track's LFO modulation sources. Add/remove
// LFOs and tweak each one's waveform, rate, depth, target, etc. Parameter
// edits are debounced (150ms) and committed per-panel via writeLfoToTree,
// which diffs each control value against the ReadModel snapshot before
// writing via projectCmds->setLfoParam — so a settled drag doesn't rewrite
// all 8 properties × N panels every tick.

#include "ModulationWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
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

    if (trackIndex < 0 || trackIndex >= readModel->getTrackCount())
    {
        trackLabel->setText("No track selected");
        clearPanels();
        return;
    }

    auto track = readModel->getTrack(trackIndex);
    trackLabel->setText(QString("Track: %1").arg(QString::fromStdString(track.name)));

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

    auto lfos = readModel->getModulationLfos(currentTrack);
    for (int i = 0; i < static_cast<int>(lfos.size()); ++i)
        addPanel(lfos[i], i);
}

int ModulationWidget::addPanel(const LfoSnapshot& lfo, int index)
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
    const int panelIdx = static_cast<int>(panels.size());

    // Waveform buttons
    auto* waveGroup = new QButtonGroup(container);
    panel.waveformGroup = waveGroup;
    QStringList waveLabels = {"Sin", "Tri", "Saw", "Sqr", "S&H"};
    for (int w = 0; w < 5; ++w)
    {
        auto* btn = new QPushButton(waveLabels[w], container);
        btn->setFixedSize(32, 22);
        btn->setCheckable(true);
        btn->setStyleSheet("font-size: 7pt;");
        waveGroup->addButton(btn, w);
        if (w == lfo.waveform) btn->setChecked(true);
        row->addWidget(btn);
    }
    connect(waveGroup, &QButtonGroup::idClicked, this, [this, panelIdx](int) {
        onLfoParamChanged(panelIdx);
    });

    // Rate
    panel.rateSpin = new QDoubleSpinBox(container);
    panel.rateSpin->setRange(0.01, 100.0);
    panel.rateSpin->setValue(lfo.rate);
    panel.rateSpin->setFixedWidth(70);
    panel.rateSpin->setSuffix(" Hz");
    row->addWidget(panel.rateSpin);

    // Sync toggle
    panel.syncBtn = new QPushButton("Sync", container);
    panel.syncBtn->setCheckable(true);
    panel.syncBtn->setChecked(lfo.rateSync);
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
    panel.depthSlider->setValue(static_cast<int>(lfo.depth * 100.0));
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
    panel.bipolarBtn->setChecked(lfo.bipolar);
    panel.bipolarBtn->setFixedSize(28, 22);
    panel.bipolarBtn->setToolTip(QString::fromUtf8("Bipolar (\xc2\xb1" "1) / Unipolar (0\xe2\x86\x92" "1)"));
    connect(panel.bipolarBtn, &QPushButton::toggled, this, [this, panelIdx](bool) {
        onLfoParamChanged(panelIdx);
    });
    row->addWidget(panel.bipolarBtn);

    // Phase offset
    panel.phaseSpin = new QDoubleSpinBox(container);
    panel.phaseSpin->setRange(0.0, 360.0);
    panel.phaseSpin->setValue(lfo.phaseOffset);
    panel.phaseSpin->setSuffix("\xc2\xb0");
    panel.phaseSpin->setFixedWidth(60);
    panel.phaseSpin->setToolTip("Phase offset");
    connect(panel.phaseSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, panelIdx](double) { onLfoParamChanged(panelIdx); });
    row->addWidget(panel.phaseSpin);

    // Target parameter
    panel.targetCombo = new QComboBox(container);
    panel.targetCombo->addItem("Volume", 1);
    panel.targetCombo->addItem("Pan", 2);
    int comboIdx = panel.targetCombo->findData(lfo.targetParamID);
    if (comboIdx >= 0) panel.targetCombo->setCurrentIndex(comboIdx);
    panel.targetCombo->setFixedWidth(90);
    connect(panel.targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, panelIdx](int) { onLfoParamChanged(panelIdx); });
    row->addWidget(panel.targetCombo);

    // Bypass
    panel.bypassBtn = new QPushButton("Byp", container);
    panel.bypassBtn->setCheckable(true);
    panel.bypassBtn->setChecked(!lfo.enabled);
    panel.bypassBtn->setFixedSize(32, 22);
    connect(panel.bypassBtn, &QPushButton::toggled, this, [this, panelIdx](bool) {
        onLfoParamChanged(panelIdx);
    });
    row->addWidget(panel.bypassBtn);

    // Remove
    panel.removeBtn = new QPushButton("\xc3\x97", container);
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

    projectCmds->beginTransaction("Add LFO");
    projectCmds->addLfo(currentTrack);
    projectCmds->endTransaction();

    rebuildPanels();
    audioGraphCmds->rebuildModulation(currentTrack);
}

void ModulationWidget::onRemoveLFO(int lfoIndex)
{
    if (currentTrack < 0) return;

    projectCmds->beginTransaction("Remove LFO");
    projectCmds->removeLfo(currentTrack, lfoIndex);
    projectCmds->endTransaction();

    rebuildPanels();
    audioGraphCmds->rebuildModulation(currentTrack);
}

bool ModulationWidget::writeLfoToTree(int lfoIndex)
{
    if (currentTrack < 0 || lfoIndex >= static_cast<int>(panels.size())) return false;

    auto lfos = readModel->getModulationLfos(currentTrack);
    if (lfoIndex >= static_cast<int>(lfos.size())) return false;

    auto& p = panels[lfoIndex];
    bool wrote = false;

    int waveId = p.waveformGroup->checkedId();
    if (waveId >= 0 && waveId != lfos[lfoIndex].waveform)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "waveform", static_cast<double>(waveId));
        wrote = true;
    }
    double rate = p.rateSpin->value();
    if (rate != lfos[lfoIndex].rate)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "rate", rate);
        wrote = true;
    }
    bool rateSync = p.syncBtn->isChecked();
    if (rateSync != lfos[lfoIndex].rateSync)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "rateSync", rateSync ? 1.0 : 0.0);
        wrote = true;
    }
    double depth = p.depthSlider->value() / 100.0;
    if (std::abs(depth - lfos[lfoIndex].depth) > 1e-9)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "depth", depth);
        wrote = true;
    }
    bool bipolar = p.bipolarBtn->isChecked();
    if (bipolar != lfos[lfoIndex].bipolar)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "bipolar", bipolar ? 1.0 : 0.0);
        wrote = true;
    }
    double phase = p.phaseSpin->value();
    if (std::abs(phase - lfos[lfoIndex].phaseOffset) > 1e-9)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "phaseOffset", phase);
        wrote = true;
    }
    int target = p.targetCombo->currentData().toInt();
    if (target != lfos[lfoIndex].targetParamID)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "targetParamID", static_cast<double>(target));
        wrote = true;
    }
    bool enabled = !p.bypassBtn->isChecked();
    if (enabled != lfos[lfoIndex].enabled)
    {
        projectCmds->setLfoParam(currentTrack, lfoIndex, "enabled", enabled ? 1.0 : 0.0);
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
    bool anyWrote = false;
    for (int i = 0; i < static_cast<int>(panels.size()); ++i)
    {
        if (!panels[i].dirty) continue;
        if (writeLfoToTree(i))
            anyWrote = true;
        panels[i].dirty = false;
    }
    if (anyWrote)
        audioGraphCmds->rebuildModulation(currentTrack);
}
