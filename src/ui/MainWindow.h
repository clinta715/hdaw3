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
#include <juce_data_structures/juce_data_structures.h>
class AudioEngine;
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../mcp/McpTransportHttp.h"

namespace mcp { class McpServer; }

class TimelineView;
class MixerWidget;
class PianoRollWidget;
class FXChainWidget;
class AutomationLaneWidget;
class AudioClipEditorWidget;
class StepEditorWidget;
class ModulationWidget;
class ProjectPoolBrowser;
class StatusBar;

class MainWindow : public QMainWindow
                 , private juce::ValueTree::Listener
{
    Q_OBJECT
public:
    MainWindow(AudioEngine& engine, QWidget* parent = nullptr);
    ~MainWindow();

    // Bottom-panel tab indices. The QStackedWidget inserts widgets in this
    // exact order (see setupBottomPanel); these named constants replace the
    // raw 0–6 magic numbers that were scattered across the signal handlers.
    // Reordering a widget's addWidget() call MUST move its entry here too.
    enum BottomPanel : int {
        Mixer = 0,
        PianoRoll = 1,
        FxChain = 2,
        Automation = 3,
        AudioEditor = 4,
        StepSequencer = 5,
        Modulation = 6,
        Count = 7
    };

    AudioEngine& getEngine() { return engine; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupLayout();
    void setupBottomPanel();
    void connectTimelineSignals();
    void connectBottomPanelSignals();
    void restoreWindowGeometry();
    void setupMenuBar();
    void rebuildAllUI();

    bool checkSaveBeforeAction();

    void onNew();
    void onOpen();
    void openProjectFile(const QString& path);
    void addToRecentProjects(const QString& path);
    void rebuildRecentProjectsMenu();
    bool onSave();
    void onSaveAs();
    void onUndo();
    void onRedo();
    void onRecordToggle();
    void onCcRecordToggled(bool armed);
    void onPlayToggle();
    void onStop();
    void onRewind();
    void onLoopToggle();
    void onExport();
    void onAddTrack();
    void onAddTrackWithFX(const juce::String& fxType);
    void onAddTrackWithPlugin(const juce::String& pluginID, const juce::String& pluginFormat);
    void onDeleteTrack();
    void onRenameTrack();
    void onDuplicateTrack();
    void onImportAudio();
    void onImportMIDI();
    int promptForImportTrack(QWidget* parent, AudioEngine& eng, const QString& title);
    void onBPMChanged(double bpm);
    void onMetronomeToggled(bool enabled);
    void onCountInToggled(bool enabled);
    void onTimeSigChanged(int numerator, int denominator);
    void onInputMonitoringChanged(int trackIndex, bool enabled);
    void onMidiDeviceChanged(const QString& deviceIdentifier);
    void onToggleBrowserPanel();
    void onClipSelected(const juce::ValueTree& clipTree);

    void startMcpHttpServer();
    void stopMcpHttpServer();

    void updateTimecode();
    void pumpJuceMessages();

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved) override;

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

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
    AudioClipEditorWidget* audioEditorWidget;
    StepEditorWidget* stepEditorWidget;
    ModulationWidget* modulationWidget = nullptr;
    StatusBar* statusBarWidget = nullptr;

    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* loopAction = nullptr;

    QTimer timecodeTimer;
    QTimer jucePumpTimer;

    juce::File currentFile;
    int selectedTrack = -1;

    QMenu* recentProjectsMenu = nullptr;

    mcp::McpServer* mcpServer_ = nullptr;
    mcp::TransportHttp* mcpHttp_ = nullptr;
    QAction* mcpHttpAction = nullptr;
};
