#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QButtonGroup>
#include <QPushButton>
#include <QList>
#include <juce_core/juce_core.h>
#include "../engine/AudioEngine.h"

class TimelineView;
class MixerWidget;
class PianoRollWidget;
class FXChainWidget;
class AutomationLaneWidget;
class ProjectPoolBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(AudioEngine& engine, QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupLayout();
    void setupMenuBar();
    void rebuildAllUI();

    bool checkSaveBeforeAction();

    void onNew();
    void onOpen();
    bool onSave();
    void onSaveAs();
    void onUndo();
    void onRedo();
    void onRecordToggle();
    void onPlayToggle();
    void onStop();
    void onRewind();
    void onExport();
    void onAddTrack();
    void onImportAudio();
    void onImportMIDI();
    void onBPMChanged(double bpm);
    void onMetronomeToggled(bool enabled);

    void updateTimecode();

    AudioEngine& engine;

    QSplitter* mainHorizontalSplitter;
    QSplitter* mainVerticalSplitter;

    TimelineView* timelineView;
    ProjectPoolBrowser* browserPanel;
    QStackedWidget* bottomStack;
    QButtonGroup* tabGroup = nullptr;
    QList<QPushButton*> tabButtons;
    MixerWidget* mixerWidget;
    PianoRollWidget* pianoRollWidget;
    FXChainWidget* fxChainWidget;
    AutomationLaneWidget* automationWidget;

    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;

    QTimer timecodeTimer;

    juce::File currentFile;
};
