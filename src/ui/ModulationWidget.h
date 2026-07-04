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
