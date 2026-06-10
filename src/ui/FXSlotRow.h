#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSlider>
#include <QLabel>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "../model/ProjectModel.h"

class AudioEngine;

class FXSlotRow : public QWidget
{
    Q_OBJECT
public:
    FXSlotRow(juce::ValueTree slotTree, int index, int trackIdx, AudioEngine& engine, QWidget* parent = nullptr);
    ~FXSlotRow() override;

    int getSlotIndex() const { return slotIndex; }
    void setSlotIndex(int idx) { slotIndex = idx; }

signals:
    void removeRequested(int index);
    void moveUpRequested(int index);
    void moveDownRequested(int index);
    void slotChanged();
    void editRequested(int index);

private:
    void populateTypeCombo();
    void rebuildParamUI();
    void onTypeChanged(const juce::String& type);

    juce::ValueTree slotTree;
    int slotIndex;
    int trackIndex;
    AudioEngine& engine;
    QComboBox* typeCombo;
    QPushButton* bypassBtn;
    QPushButton* editBtn;
    QWidget* paramContainer;
    bool bypassed = false;

    struct ParamSlider {
        QSlider* slider;
        QLabel* valueLabel;
        int paramIdx;
    };
    std::vector<ParamSlider> paramSliders;
};
