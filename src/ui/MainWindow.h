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
#include "../mcp/McpTransportHttp.h"

namespace mcp { class McpServer; }

class TimelineView;
class MixerWidget;
class PianoRollWidget;
class FXChainWidget;
class AutomationLaneWidget;
class AudioClipEditorWidget;
class StepEditorWidget;
class ProjectPoolBrowser;

class MainWindow : public QMainWindow
                 , private juce::ValueTree::Listener
{
    Q_OBJECT
public:
    MainWindow(AudioEngine& engine, QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupLayout();
    void setupBottomPanel();
    void connectSignals();
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
    void onBPMChanged(double bpm);
    void onMetronomeToggled(bool enabled);
    void onToggleBrowserPanel();
    void onClipSelected(const juce::ValueTree& clipTree);

    void startMcpHttpServer();
    void stopMcpHttpServer();

    void updateTimecode();
    void pumpJuceMessages();

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

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
    AudioClipEditorWidget* audioEditorWidget;
    StepEditorWidget* stepEditorWidget;

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
