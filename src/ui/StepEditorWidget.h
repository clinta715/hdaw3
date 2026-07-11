#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class StepEditorWidget : public QWidget
{
    Q_OBJECT
public:
    StepEditorWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~StepEditorWidget() override;

    void loadClip(juce::ValueTree clipTree);
    void clear();
    bool hasClip() const { return isLoaded; }
    juce::ValueTree getClipTree() const { return currentClip; }

    static constexpr int numRows = 8;
    static constexpr int numSteps = 16;
    static constexpr int noteStart = 48; // C3

signals:
    void clipClosed();
    void switchToPianoRoll();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void syncFromClip();
    void toggleStep(int row, int step);
    void commitNote(int row, int step, bool add);

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    juce::ValueTree currentClip;
    bool isLoaded = false;

    QLabel* titleLabel;
    QPushButton* closeButton;
    QPushButton* switchButton;

    // grid state: which steps are active per row
    bool stepGrid[numRows][numSteps] = {};

    // cached layout
    int headerH = 28;
    int rowH = 28;
    int stepW = 28;
    int labelW = 40;
};
