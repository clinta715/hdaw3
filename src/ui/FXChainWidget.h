#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include "../engine/AudioEngine.h"
#include "FXSlotRow.h"

class FXChainWidget : public QWidget
{
    Q_OBJECT
public:
    FXChainWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~FXChainWidget() override;

    void loadTrack(int trackIndex);
    void clear();

signals:
    void chainChanged();

private:
    void rebuildUI();
    void addFXSlot(const juce::String& type = "eq");

    AudioEngine& engine;
    int currentTrack = -1;
    QVBoxLayout* slotLayout;
    QScrollArea* scrollArea;
    QWidget* slotContainer;
    QLabel* headerLabel;
};
