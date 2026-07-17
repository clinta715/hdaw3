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
#include <QTimer>
#include <juce_core/juce_core.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;
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
    void onLfoParamChanged(int lfoIndex);
    void flushChanges();

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
        // Set when any control on this panel changes since the last flush,
        // so flushChanges() only commits (and rebuilds audio for) panels that
        // actually changed — not every panel on every 150ms tick.
        bool dirty = false;
    };

    void clearPanels();
    void rebuildPanels();
    int addPanel(const LfoSnapshot& lfo, int index);
    bool writeLfoToTree(int lfoIndex);

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    int currentTrack = -1;
    QVBoxLayout* listLayout;
    QWidget* listWidget;
    QPushButton* addBtn;
    QLabel* trackLabel;
    std::vector<LfoPanel> panels;
    QTimer debounceTimer;
};
