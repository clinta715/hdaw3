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
    connect(panel.syncBtn, &QPushButton::toggled, this, [this, &panel](bool checked) mutable {
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

    // Use index-based lookup instead of reference capture
    int panelIdx = static_cast<int>(panels.size());
    connect(panel.depthSlider, &QSlider::valueChanged, this, [this, panelIdx](int v) {
        if (panelIdx < static_cast<int>(panels.size()))
        {
            panels[panelIdx].depthLabel->setText(QString("%1%").arg(v));
        }
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
