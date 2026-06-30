#include "MainWindow.h"
#include "TimelineView.h"
#include "MixerWidget.h"
#include "PianoRollWidget.h"
#include "FXChainWidget.h"
#include "AutomationLaneWidget.h"
#include "AudioClipEditorWidget.h"
#include "StepEditorWidget.h"
#include "StartupDialog.h"
#include "AboutDialog.h"
#include "PhraseGeneratorDialog.h"
#include "ProjectPoolBrowser.h"
#include "VUMeter.h"
#include "../engine/ProjectSerializer.h"
#include "ExportDialog.h"
#include "PluginScannerDialog.h"
#include "PreferencesDialog.h"
#include "DebugLog.h"
#include "../mcp/McpServer.h"
#include "../mcp/McpTools.h"
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QApplication>
#include <QCloseEvent>
#include <QMenuBar>
#include <QShortcut>
#include <QInputDialog>

// Keep this in sync with VERSION in CMakeLists.txt.
static constexpr const char* APP_VERSION = "0.3.0";

MainWindow::MainWindow(AudioEngine& ae, QWidget* parent)
    : QMainWindow(parent), engine(ae)
{
    setWindowTitle(QString("HDAW %1 - Untitled").arg(APP_VERSION));
    resize(1200, 800);

    mcpServer_ = new mcp::McpServer(this);
    mcpServer_->setEngine(&engine);
    engine.getProjectModel().setPluginManager(&engine.getPluginManager());
    mcp::registerAllTools(*mcpServer_);

    setupLayout();
    setupMenuBar();

    // Load saved preferences
    double clipDur = PreferencesDialog::getDefaultClipDuration();
    timelineView->getToolbar()->setDefaultClipLen(clipDur);
    timelineView->getInteraction()->setDefaultClipDuration(clipDur);

    auto* toolbar = timelineView->getToolbar();
    toolbar->addTrackPluginMenu(nullptr, engine.getPluginManager());

    toolbar->setSnap(PreferencesDialog::getSnapEnabled());
    toolbar->setSnapDivision(PreferencesDialog::getSnapDivision());

    // Listen for transport property changes (loop toggle, etc.)
    engine.getProjectModel().getTransportTree().addListener(this);

    // Startup dialog — let user choose new/open/recent before the main window appears
    {
        StartupDialog startup(this);
        startup.exec();

        switch (startup.getAction())
        {
        case StartupDialog::NewProject:
            onNew();
            break;
        case StartupDialog::OpenRecent:
            openProjectFile(startup.getSelectedPath());
            break;
        case StartupDialog::OpenOther:
            onOpen();
            break;
        case StartupDialog::Cancel:
        default:
            break; // keep default project
        }
    }
}

MainWindow::~MainWindow()
{
    if (mcpHttp_ != nullptr)
    {
        mcpHttp_->stop();
        delete mcpHttp_;
        mcpHttp_ = nullptr;
    }

    auto transportTree = engine.getProjectModel().getTransportTree();
    if (transportTree.isValid())
        transportTree.removeListener(this);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    timecodeTimer.stop();
    if (checkSaveBeforeAction())
    {
        auto& settings = PreferencesDialog::settings();
        settings.setValue(PreferencesDialog::kKeyWindowGeometry, saveGeometry());
        settings.setValue(PreferencesDialog::kKeyWindowState, saveState());
        if (mainHorizontalSplitter != nullptr)
            settings.setValue(PreferencesDialog::kKeyHorizontalSplitter, mainHorizontalSplitter->saveState());
        if (mainVerticalSplitter != nullptr)
            settings.setValue(PreferencesDialog::kKeyVerticalSplitter, mainVerticalSplitter->saveState());
        if (bottomStack != nullptr)
            settings.setValue(PreferencesDialog::kKeyBottomPanelIndex, bottomStack->currentIndex());
        event->accept();
    }
    else
    {
        timecodeTimer.start(33);
        event->ignore();
    }
}

void MainWindow::setupMenuBar()
{
    // ── File ──
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newAction = fileMenu->addAction(tr("&New Project"), this, &MainWindow::onNew);
    newAction->setShortcut(QKeySequence::New);

    auto* openAction = fileMenu->addAction(tr("&Open..."), this, &MainWindow::onOpen);
    openAction->setShortcut(QKeySequence::Open);

    recentProjectsMenu = fileMenu->addMenu(tr("Open &Recent"));
    rebuildRecentProjectsMenu();

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction(tr("&Save"), this, &MainWindow::onSave);
    saveAction->setShortcut(QKeySequence::Save);

    auto* saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::onSaveAs);
    saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

    fileMenu->addSeparator();

    auto* importAudioAction = fileMenu->addAction(tr("Import &Audio..."), this, &MainWindow::onImportAudio);
    importAudioAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));

    auto* importMidiAction = fileMenu->addAction(tr("Import &MIDI..."), this, &MainWindow::onImportMIDI);
    importMidiAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));

    fileMenu->addSeparator();

    auto* exportAction = fileMenu->addAction(tr("Export &Audio..."), this, &MainWindow::onExport);
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));

    fileMenu->addSeparator();

    auto* exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    // ── Edit ──
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    undoAction = editMenu->addAction(tr("&Undo"), this, &MainWindow::onUndo);
    undoAction->setShortcut(QKeySequence::Undo);

    redoAction = editMenu->addAction(tr("&Redo"), this, &MainWindow::onRedo);
    redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));

    // ── Transport ──
    auto* transportMenu = menuBar()->addMenu(tr("&Transport"));

    auto* playAction = transportMenu->addAction(tr("&Play / Stop"));
    playAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(playAction, &QAction::triggered, this, &MainWindow::onPlayToggle);

    auto* stopAction = transportMenu->addAction(tr("Sto&p"), this, &MainWindow::onStop);
    stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Space));

    auto* recordAction = transportMenu->addAction(tr("&Record"));
    recordAction->setShortcut(QKeySequence(Qt::Key_R));
    connect(recordAction, &QAction::triggered, this, &MainWindow::onRecordToggle);

    transportMenu->addSeparator();

    auto* rewindAction = transportMenu->addAction(tr("&Rewind to Start"), this, &MainWindow::onRewind);
    rewindAction->setShortcut(QKeySequence(Qt::Key_Home));

    loopAction = transportMenu->addAction(tr("&Loop"), this, &MainWindow::onLoopToggle);
    loopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    loopAction->setCheckable(true);

    // ── Track ──
    auto* trackMenu = menuBar()->addMenu(tr("&Track"));

    auto* addTrackAction = trackMenu->addAction(tr("&Add Track"), this, &MainWindow::onAddTrack);
    addTrackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    auto* deleteTrackAction = trackMenu->addAction(tr("&Delete Track"), this, &MainWindow::onDeleteTrack);
    deleteTrackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));

    auto* renameTrackAction = trackMenu->addAction(tr("&Rename Track"), this, &MainWindow::onRenameTrack);
    renameTrackAction->setShortcut(QKeySequence(Qt::Key_F2));

    auto* dupTrackAction = trackMenu->addAction(tr("&Duplicate Track"), this, &MainWindow::onDuplicateTrack);
    dupTrackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

    // ── View ──
    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* toggleBrowserAction = viewMenu->addAction(tr("Toggle &Project Pool"),
        this, &MainWindow::onToggleBrowserPanel);
    toggleBrowserAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));

    viewMenu->addSeparator();

    auto* zoomInViewAction = viewMenu->addAction(tr("Zoom &In"));
    zoomInViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    connect(zoomInViewAction, &QAction::triggered, timelineView, &TimelineView::zoomIn);

    auto* zoomOutViewAction = viewMenu->addAction(tr("Zoom &Out"));
    zoomOutViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(zoomOutViewAction, &QAction::triggered, timelineView, &TimelineView::zoomOut);

    // ── Tools ──
    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));

    auto* pluginMgrAction = toolsMenu->addAction(tr("Plugin &Manager..."));
    pluginMgrAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(pluginMgrAction, &QAction::triggered, this, [this]() {
        PluginScannerDialog dialog(engine, this);
        dialog.exec();
    });

    auto* phraseGenAction = toolsMenu->addAction(tr("Phrase &Generator..."));
    phraseGenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(phraseGenAction, &QAction::triggered, this, [this]() {
        int trackIdx = selectedTrack;
        if (trackIdx < 0) {
            statusBar()->showMessage("Select a track first.", 3000);
            return;
        }
        PhraseGeneratorDialog dialog(engine, trackIdx, this);
        dialog.exec();
    });

    toolsMenu->addSeparator();

    mcpHttpAction = toolsMenu->addAction(tr("&MCP HTTP Server"));
    mcpHttpAction->setCheckable(true);
    {
        auto& settings = PreferencesDialog::settings();
        mcpHttpAction->setChecked(settings.value("mcp/httpEnabled", false).toBool());
    }
    connect(mcpHttpAction, &QAction::toggled, this, [this](bool on) {
        auto& settings = PreferencesDialog::settings();
        settings.setValue("mcp/httpEnabled", on);
        if (on) startMcpHttpServer();
        else    stopMcpHttpServer();
    });

    if (mcpHttpAction->isChecked()) startMcpHttpServer();

    // ── Help ──
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    auto* aboutAction = helpMenu->addAction(tr("&About HDAW..."));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });

    toolsMenu->addSeparator();

    auto* prefAction = toolsMenu->addAction(tr("&Preferences..."));
    prefAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(prefAction, &QAction::triggered, this, [this]() {
        PreferencesDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            double clipDur = PreferencesDialog::getDefaultClipDuration();
            timelineView->getToolbar()->setDefaultClipLen(clipDur);
            timelineView->getInteraction()->setDefaultClipDuration(clipDur);
        }
    });
}

void MainWindow::startMcpHttpServer()
{
    if (mcpHttp_ != nullptr) return;
    auto& settings = PreferencesDialog::settings();
    quint16 port = static_cast<quint16>(settings.value("mcp/httpPort", 8765).toInt());
    mcpHttp_ = new mcp::TransportHttp(port);
    mcpHttp_->start(mcpServer_);
    if (mcpHttpAction != nullptr && !mcpHttpAction->isChecked())
    {
        const QSignalBlocker b(mcpHttpAction);
        mcpHttpAction->setChecked(true);
    }
    statusBar()->showMessage(QString("MCP HTTP server listening on 127.0.0.1:%1").arg(port), 5000);
}

void MainWindow::stopMcpHttpServer()
{
    if (mcpHttp_ == nullptr) return;
    mcpHttp_->stop();
    delete mcpHttp_;
    mcpHttp_ = nullptr;
    if (mcpHttpAction != nullptr && mcpHttpAction->isChecked())
    {
        const QSignalBlocker b(mcpHttpAction);
        mcpHttpAction->setChecked(false);
    }
    statusBar()->showMessage("MCP HTTP server stopped", 3000);
}

void MainWindow::setupLayout()
{
    setStatusBar(new QStatusBar(this));
    statusBar()->showMessage("Ready");

    timelineView = new TimelineView(engine, this);

    browserPanel = new ProjectPoolBrowser(engine, this);

    auto* bottomContainer = new QFrame(this);
    bottomContainer->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    auto* bottomLayout = new QVBoxLayout(bottomContainer);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    // Tab bar
    auto* tabBar = new QWidget(bottomContainer);
    tabBar->setFixedHeight(24);
    auto* tabLayout = new QHBoxLayout(tabBar);
    tabLayout->setContentsMargins(4, 2, 4, 2);
    tabLayout->setSpacing(2);

    tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);

    auto makeTab = [&](const QString& label, int index) {
        auto* btn = new QPushButton(label, tabBar);
        btn->setFixedHeight(20);
        btn->setCheckable(true);
        tabGroup->addButton(btn, index);
        tabButtons.append(btn);
        tabLayout->addWidget(btn);
        return btn;
    };

    auto* mixerTab = makeTab("Mixer", 0);
    auto* pianoTab = makeTab("Piano Roll", 1);
    auto* fxTab = makeTab("FX Chain", 2);
    auto* autoTab = makeTab("Automation", 3);
    auto* audioTab = makeTab("Audio Editor", 4);
    auto* stepTab = makeTab("Step Seq", 5);
    juce::ignoreUnused(mixerTab, pianoTab, fxTab, autoTab, audioTab, stepTab);

    tabLayout->addStretch();
    bottomLayout->addWidget(tabBar);

    bottomStack = new QStackedWidget(bottomContainer);

    mixerWidget = new MixerWidget(engine, bottomStack);
    bottomStack->addWidget(mixerWidget);

    pianoRollWidget = new PianoRollWidget(engine, bottomStack);
    bottomStack->addWidget(pianoRollWidget);

    fxChainWidget = new FXChainWidget(engine, bottomStack);
    bottomStack->addWidget(fxChainWidget);

    automationWidget = new AutomationLaneWidget(engine, bottomStack);
    bottomStack->addWidget(automationWidget);

    audioEditorWidget = new AudioClipEditorWidget(engine, bottomStack);
    bottomStack->addWidget(audioEditorWidget);

    stepEditorWidget = new StepEditorWidget(engine, bottomStack);
    bottomStack->addWidget(stepEditorWidget);

    // Connect tab button clicks to stack switching
    connect(tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 2 && selectedTrack >= 0)
            fxChainWidget->loadTrack(selectedTrack);
        if (id == 3 && selectedTrack >= 0)
            automationWidget->loadTrack(selectedTrack);
        bottomStack->setCurrentIndex(id);
    });

    // Keep tab button checked-state in sync with the stack — covers both
    // user clicks on the tab and programmatic setCurrentIndex (e.g. from clipSelected).
    connect(bottomStack, &QStackedWidget::currentChanged, this, [this](int index) {
        if (index < 0 || index >= tabButtons.size()) return;
        for (int i = 0; i < tabButtons.size(); ++i)
            tabButtons[i]->setChecked(i == index);
    });
    // Set the initial checked state to match the stack's starting index.
    if (bottomStack->currentIndex() >= 0 && bottomStack->currentIndex() < tabButtons.size())
        tabButtons[bottomStack->currentIndex()]->setChecked(true);

    bottomStack->setCurrentIndex(0);

    bottomLayout->addWidget(bottomStack);

    mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
    mainVerticalSplitter->addWidget(timelineView);
    mainVerticalSplitter->addWidget(bottomContainer);
    mainVerticalSplitter->setStretchFactor(0, 3);
    mainVerticalSplitter->setStretchFactor(1, 1);

    mainHorizontalSplitter = new QSplitter(Qt::Horizontal, this);
    mainHorizontalSplitter->addWidget(mainVerticalSplitter);
    mainHorizontalSplitter->addWidget(browserPanel);
    mainHorizontalSplitter->setStretchFactor(0, 4);
    mainHorizontalSplitter->setStretchFactor(1, 1);

    setCentralWidget(mainHorizontalSplitter);

    auto* scene = timelineView->getScene();
    connect(scene, &TimelineScene::clipSelected, this,
        [this](juce::ValueTree clipTree) {
            HDAW_LOG("MWClipSel", QString("ENTER type=%1 valid=%2")
                .arg(QString::fromUtf8(clipTree.getProperty(IDs::clipType).toString().toRawUTF8()))
                .arg(clipTree.isValid() ? 1 : 0));
            auto trackTree = ProjectModel::getTrackOfClip(clipTree);
            if (trackTree.isValid() && trackTree.hasType(IDs::TRACK))
            {
                auto trackList = engine.getProjectModel().getTrackListTree();
                int trackIdx = trackList.indexOf(trackTree);
                if (trackIdx >= 0)
                {
                    selectedTrack = trackIdx;
                    timelineView->selectTrack(trackIdx);
                }
            }

            juce::String type = clipTree.getProperty(IDs::clipType).toString();
            if (type == "midi")
            {
                auto& settings = PreferencesDialog::settings();
                QString mode = settings.value("midiEditorMode", "piano").toString();
                if (mode == "step")
                {
                    stepEditorWidget->loadClip(clipTree);
                    bottomStack->setCurrentIndex(5);
                }
                else
                {
                    auto vs = mainVerticalSplitter->sizes();
                    HDAW_LOG("MWClipSel", QString("piano: before load mode=%1 vSplit=[%2,%3] rollH=%4 rollVis=%5")
                        .arg(mode)
                        .arg(vs.value(0)).arg(vs.value(1))
                        .arg(pianoRollWidget->height())
                        .arg(pianoRollWidget->isVisible() ? 1 : 0));
                    pianoRollWidget->loadClip(clipTree);
                    bottomStack->setCurrentIndex(1);
                    HDAW_LOG("MWClipSel", QString("piano: after load stackIdx=%1 rollH=%2 rollVis=%3")
                        .arg(bottomStack->currentIndex())
                        .arg(pianoRollWidget->height())
                        .arg(pianoRollWidget->isVisible() ? 1 : 0));
                }
            }
            else
            {
                audioEditorWidget->loadClip(clipTree);
                bottomStack->setCurrentIndex(4);
            }
        });

    connect(pianoRollWidget, &PianoRollWidget::clipClosed, this,
        [this]() {
            pianoRollWidget->clear();
            bottomStack->setCurrentIndex(0);
        });

    connect(audioEditorWidget, &AudioClipEditorWidget::clipClosed, this,
        [this]() {
            audioEditorWidget->clear();
            bottomStack->setCurrentIndex(0);
        });

    connect(stepEditorWidget, &StepEditorWidget::clipClosed, this,
        [this]() {
            stepEditorWidget->clear();
            bottomStack->setCurrentIndex(0);
        });

    connect(stepEditorWidget, &StepEditorWidget::switchToPianoRoll, this,
        [this]() {
            if (stepEditorWidget->hasClip())
            {
                // Reload current clip in piano roll, same track
                pianoRollWidget->loadClip(stepEditorWidget->getClipTree());
                bottomStack->setCurrentIndex(1);
            }
        });

    connect(mixerWidget, &MixerWidget::fxButtonClicked, this,
        [this](int trackIndex) {
            selectedTrack = trackIndex;
            fxChainWidget->loadTrack(trackIndex);
            bottomStack->setCurrentIndex(2);
        });

    connect(timelineView, &TimelineView::automationToggled, this,
        [this](int trackIndex) {
            selectedTrack = trackIndex;
            automationWidget->loadTrack(trackIndex);
            bottomStack->setCurrentIndex(3);
        });

    connect(timelineView, &TimelineView::trackSelectionChanged, this,
        [this](int trackIndex) {
            selectedTrack = trackIndex;
            int idx = bottomStack->currentIndex();
            if (idx == 2 && selectedTrack >= 0)
                fxChainWidget->loadTrack(trackIndex);
            if (idx == 3 && selectedTrack >= 0)
                automationWidget->loadTrack(trackIndex);
        });

    connect(timelineView, &TimelineView::addTrackClicked, this, &MainWindow::onAddTrack);
    connect(timelineView, &TimelineView::addTrackWithFX, this, &MainWindow::onAddTrackWithFX);
    connect(timelineView, &TimelineView::addTrackWithPlugin, this, &MainWindow::onAddTrackWithPlugin);
    connect(timelineView, &TimelineView::bpmChanged, this, &MainWindow::onBPMChanged);
    connect(timelineView, &TimelineView::metronomeToggled, this, &MainWindow::onMetronomeToggled);

    connect(timelineView, &TimelineView::recordToggled, this, &MainWindow::onRecordToggle);
    connect(timelineView, &TimelineView::playToggled, this, &MainWindow::onPlayToggle);
    connect(timelineView, &TimelineView::stopRequested, this, &MainWindow::onStop);
    connect(timelineView, &TimelineView::rewindRequested, this, &MainWindow::onRewind);

    // Timecode timer
    connect(&timecodeTimer, &QTimer::timeout, this, &MainWindow::updateTimecode);
    timecodeTimer.start(33);

    // JUCE message pump (needed to drive JUCE timers/async updates in a Qt app)
    connect(&jucePumpTimer, &QTimer::timeout, this, &MainWindow::pumpJuceMessages);
    jucePumpTimer.start(10);

    connect(fxChainWidget, &FXChainWidget::chainChanged, this,
        [this]() {
            auto trackList = engine.getProjectModel().getTrackListTree();
            auto* routing = engine.getMainProcessor()->getRoutingManager();
            if (routing == nullptr) return;
            int numTracks = (std::min)(trackList.getNumChildren(), routing->getNumTracks());
            for (int i = 0; i < numTracks; ++i)
                engine.getMainProcessor()->rebuildTrackFX(i);
        });

    connect(automationWidget, &AutomationLaneWidget::automationChanged, this,
        [this]() {
            int track = automationWidget->currentTrackIndex();
            if (track >= 0)
                engine.getMainProcessor()->rebuildAutomationCache(track);
        });

    // Restore saved window and panel state from previous session
    {
        auto& settings = PreferencesDialog::settings();
        auto geometry = settings.value(PreferencesDialog::kKeyWindowGeometry);
        if (geometry.isValid())
            restoreGeometry(geometry.toByteArray());
        auto winState = settings.value(PreferencesDialog::kKeyWindowState);
        if (winState.isValid())
            restoreState(winState.toByteArray());
        if (mainHorizontalSplitter != nullptr)
        {
            auto hState = settings.value(PreferencesDialog::kKeyHorizontalSplitter);
            if (hState.isValid())
                mainHorizontalSplitter->restoreState(hState.toByteArray());
        }
        if (mainVerticalSplitter != nullptr)
        {
            auto vState = settings.value(PreferencesDialog::kKeyVerticalSplitter);
            if (vState.isValid())
                mainVerticalSplitter->restoreState(vState.toByteArray());
        }
        if (bottomStack != nullptr)
        {
            auto panelIdx = settings.value(PreferencesDialog::kKeyBottomPanelIndex, 0);
            int idx = panelIdx.toInt();
            if (idx >= 0 && idx < bottomStack->count())
            {
                bottomStack->setCurrentIndex(idx);
                if (idx == 2 && selectedTrack >= 0)
                    fxChainWidget->loadTrack(selectedTrack);
                if (idx == 3 && selectedTrack >= 0)
                    automationWidget->loadTrack(selectedTrack);
            }
        }
    }
}

void MainWindow::rebuildAllUI()
{
    timelineView->getScene()->rebuildFromValueTree();
    mixerWidget->rebuild();
    fxChainWidget->clear();
    pianoRollWidget->clear();
    audioEditorWidget->clear();
    automationWidget->clear();

    auto trackList = engine.getProjectModel().getTrackListTree();
    int numTracks = trackList.getNumChildren();

    // Ensure all track FX are built in the engine
    for (int i = 0; i < numTracks; ++i)
        engine.getMainProcessor()->rebuildTrackFX(i);

    if (selectedTrack >= numTracks)
        selectedTrack = numTracks - 1;

    if (selectedTrack >= 0)
    {
        int idx = bottomStack->currentIndex();
        if (idx == 2)
            fxChainWidget->loadTrack(selectedTrack);
        else if (idx == 3)
            automationWidget->loadTrack(selectedTrack);
    }

    statusBar()->showMessage("Project loaded", 3000);
}

bool MainWindow::checkSaveBeforeAction()
{
    if (!engine.getProjectModel().isDirty())
        return true;

    auto result = QMessageBox::question(this, "Unsaved Changes",
        "Do you want to save changes before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save)
        return onSave();
    if (result == QMessageBox::Discard)
        return true;
    return false;
}

void MainWindow::onNew()
{
    if (!checkSaveBeforeAction()) return;

    HDAW::ProjectSerializer::createNew(engine.getProjectModel());
    currentFile = {};
    engine.getMainProcessor()->rebuildRoutingGraph();
    rebuildAllUI();
    setWindowTitle(QString("HDAW %1 - Untitled").arg(APP_VERSION));
}

void MainWindow::onOpen()
{
    if (!checkSaveBeforeAction()) return;

    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Open Project",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "HDAW Projects (*.hdaw)");
    if (path.isEmpty()) return;

    openProjectFile(path);
}

void MainWindow::openProjectFile(const QString& path)
{
    if (path.isEmpty()) return;

    if (!QFileInfo(path).exists())
    {
        QMessageBox::warning(this, "File Not Found",
            QString("The file no longer exists:\n%1").arg(path));
        auto& settings = PreferencesDialog::settings();
        QStringList list = settings.value(PreferencesDialog::kKeyRecentProjects).toStringList();
        if (list.removeAll(path) > 0)
        {
            settings.setValue(PreferencesDialog::kKeyRecentProjects, list);
            rebuildRecentProjectsMenu();
        }
        return;
    }

    juce::File file(path.toUtf8().constData());
    if (!HDAW::ProjectSerializer::load(engine.getProjectModel(), file))
    {
        QMessageBox::warning(this, "Error", "Failed to load project file.");
        return;
    }

    auto& settings = PreferencesDialog::settings();
    settings.setValue(PreferencesDialog::kKeyLastProjectDir, QFileInfo(path).absolutePath());
    currentFile = file;
    engine.getMainProcessor()->rebuildRoutingGraph();
    rebuildAllUI();
    setWindowTitle(QString("HDAW %1 - %2").arg(APP_VERSION)
        .arg(QString::fromUtf8(file.getFileName().toRawUTF8())));

    addToRecentProjects(path);
}

void MainWindow::addToRecentProjects(const QString& path)
{
    if (path.isEmpty()) return;

    auto& settings = PreferencesDialog::settings();
    QStringList list = settings.value(PreferencesDialog::kKeyRecentProjects).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8) list.removeLast();
    settings.setValue(PreferencesDialog::kKeyRecentProjects, list);
    rebuildRecentProjectsMenu();
}

void MainWindow::rebuildRecentProjectsMenu()
{
    if (recentProjectsMenu == nullptr) return;

    recentProjectsMenu->clear();

    auto& settings = PreferencesDialog::settings();
    QStringList list = settings.value(PreferencesDialog::kKeyRecentProjects).toStringList();

    if (list.isEmpty())
    {
        auto* empty = recentProjectsMenu->addAction(tr("(No recent projects)"));
        empty->setEnabled(false);
        return;
    }

    for (const auto& path : list)
    {
        recentProjectsMenu->addAction(path, this, [this, path]() {
            openProjectFile(path);
        });
    }

    recentProjectsMenu->addSeparator();
    recentProjectsMenu->addAction(tr("&Clear Recent"), this, [this]() {
        QSettings s(PreferencesDialog::kSettingsOrg, PreferencesDialog::kSettingsApp);
        s.remove(PreferencesDialog::kKeyRecentProjects);
        rebuildRecentProjectsMenu();
    });
}

bool MainWindow::onSave()
{
    if (currentFile.existsAsFile())
        return HDAW::ProjectSerializer::save(engine.getProjectModel(), currentFile);

    onSaveAs();
    return currentFile.existsAsFile();
}

void MainWindow::onSaveAs()
{
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getSaveFileName(this, "Save Project As",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "HDAW Projects (*.hdaw)");
    if (path.isEmpty()) return;

    juce::File file(path.toUtf8().constData());
    if (!HDAW::ProjectSerializer::save(engine.getProjectModel(), file))
    {
        QMessageBox::warning(this, "Error", "Failed to save project file.");
        return;
    }

    settings.setValue(PreferencesDialog::kKeyLastProjectDir,
        QFileInfo(path).absolutePath());
    currentFile = file;
    setWindowTitle(QString("HDAW %1 - %2").arg(APP_VERSION)
        .arg(QString::fromUtf8(file.getFileName().toRawUTF8())));
    statusBar()->showMessage("Project saved", 3000);
}

void MainWindow::onUndo()
{
    auto& um = engine.getProjectModel().getUndoManager();
    if (um.canUndo())
    {
        um.undo();
        rebuildAllUI();
    }
}

void MainWindow::onRedo()
{
    auto& um = engine.getProjectModel().getUndoManager();
    if (um.canRedo())
    {
        um.redo();
        rebuildAllUI();
    }
}

void MainWindow::onPlayToggle()
{
    auto& tm = engine.getTransportManager();
    if (tm.isPlayingNow())
    {
        tm.setPlaying(false);
        statusBar()->showMessage("Stopped", 3000);
    }
    else
    {
        tm.setPlaying(true);
        statusBar()->showMessage("Playing", 3000);
    }
}

void MainWindow::onStop()
{
    auto& tm = engine.getTransportManager();
    if (engine.getMainProcessor()->isRecording())
        engine.getMainProcessor()->stopRecording();
    tm.setPlaying(false);
    statusBar()->showMessage("Stopped", 3000);
}

void MainWindow::onRewind()
{
    auto& tm = engine.getTransportManager();
    tm.setCurrentSample(0);
}

void MainWindow::onLoopToggle()
{
    auto transportTree = engine.getProjectModel().getTransportTree();
    bool current = transportTree.getProperty(IDs::isLooping);
    bool next = !current;
    transportTree.setProperty(IDs::isLooping, next,
        &engine.getProjectModel().getUndoManager());

    if (loopAction)
    {
        loopAction->blockSignals(true);
        loopAction->setChecked(next);
        loopAction->blockSignals(false);
    }
}

void MainWindow::updateTimecode()
{
    auto& tm = engine.getTransportManager();
    int64_t samples = tm.getCurrentSample();
    double sr = tm.getSampleRate();
    double seconds = static_cast<double>(samples) / sr;

    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int millis = static_cast<int>((seconds - static_cast<double>(static_cast<int>(seconds))) * 1000.0);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%03d", mins, secs, millis);

    timelineView->getToolbar()->setTimecode(QString::fromUtf8(buf));
    timelineView->getToolbar()->setPlaying(tm.isPlayingNow());
    timelineView->getToolbar()->setBPM(tm.getBPM());

    timelineView->scrollToPlayhead();
}

void MainWindow::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType(IDs::TRANSPORT) && property == IDs::isLooping)
    {
        bool looping = tree.getProperty(IDs::isLooping);
        timelineView->getToolbar()->setLoopEnabled(looping);
        if (loopAction)
        {
            loopAction->blockSignals(true);
            loopAction->setChecked(looping);
            loopAction->blockSignals(false);
        }
    }
}

void MainWindow::onExport()
{
    ExportDialog dialog(engine, this);
    dialog.exec();
}

void MainWindow::onAddTrack()
{
    HDAW_LOG("MWAddTrk", "ENTER");
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    int before = trackList.getNumChildren();
    HDAW_LOG("MWAddTrk", QString("before count=%1").arg(before));

    int trackNum = trackList.getNumChildren() + 1;

    juce::ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, ("Track " + juce::String(trackNum)).toRawUTF8(), &model.getUndoManager());
    track.setProperty(IDs::volume, 0.85, &model.getUndoManager());
    track.setProperty(IDs::pan, 0.0, &model.getUndoManager());
    track.setProperty(IDs::isMuted, false, &model.getUndoManager());
    track.setProperty(IDs::isSoloed, false, &model.getUndoManager());
    track.setProperty(IDs::parentBus, 0, &model.getUndoManager());
    track.setProperty(IDs::color, static_cast<int>(model.trackColorForIndex(before)), &model.getUndoManager());

    juce::ValueTree clipList(IDs::CLIP_LIST);
    track.addChild(clipList, -1, &model.getUndoManager());

    juce::ValueTree fxChain(IDs::FX_CHAIN);
    track.addChild(fxChain, -1, &model.getUndoManager());

    juce::ValueTree autoList(IDs::AUTOMATION_LIST);
    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, "Volume", nullptr);
    autoTree.setProperty(IDs::paramID, 1, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);
    juce::ValueTree pointList(IDs::POINT_LIST);
    juce::ValueTree point1(IDs::POINT);
    point1.setProperty(IDs::startTime, 0.0, nullptr);
    point1.setProperty(IDs::gain, 1.0, nullptr);
    pointList.addChild(point1, -1, nullptr);
    juce::ValueTree point2(IDs::POINT);
    point2.setProperty(IDs::startTime, 16.0, nullptr);
    point2.setProperty(IDs::gain, 1.0, nullptr);
    pointList.addChild(point2, -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);
    autoList.addChild(autoTree, -1, nullptr);
    track.addChild(autoList, -1, &model.getUndoManager());

    trackList.addChild(track, -1, &model.getUndoManager());

    int after = trackList.getNumChildren();
    HDAW_LOG("MWAddTrk", QString("after count=%1 (expected %2)")
        .arg(after).arg(before + 1));

    engine.getMainProcessor()->rebuildRoutingGraph();
    rebuildAllUI();
    HDAW_LOG("MWAddTrk", "EXIT");
}

void MainWindow::onAddTrackWithFX(const juce::String& fxType)
{
    onAddTrack();

    auto trackList = engine.getProjectModel().getTrackListTree();
    int last = trackList.getNumChildren() - 1;
    if (last < 0) return;

    engine.getProjectModel().addFxSlot(last, fxType.toStdString());

    engine.getMainProcessor()->rebuildTrackFX(last);
    rebuildAllUI();
}

void MainWindow::onAddTrackWithPlugin(const juce::String& pluginID, const juce::String& /*pluginFormat*/)
{
    onAddTrack();

    auto trackList = engine.getProjectModel().getTrackListTree();
    int last = trackList.getNumChildren() - 1;
    if (last < 0) return;

    engine.getProjectModel().addFxSlot(last, "plugin", -1, pluginID.toStdString());

    engine.getMainProcessor()->rebuildTrackFX(last);
    rebuildAllUI();
}

void MainWindow::onDeleteTrack()
{
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackList.getNumChildren() <= 0) return;

    int last = trackList.getNumChildren() - 1;
    trackList.removeChild(trackList.getChild(last), &model.getUndoManager());
    engine.getMainProcessor()->rebuildRoutingGraph();
    rebuildAllUI();
}

void MainWindow::onRenameTrack()
{
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackList.getNumChildren() <= 0) return;

    int last = trackList.getNumChildren() - 1;
    auto tree = trackList.getChild(last);
    QString current = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename Track", "Track name:",
        QLineEdit::Normal, current, &ok);
    if (ok && !newName.isEmpty())
    {
        tree.setProperty(IDs::name, newName.toUtf8().constData(), &model.getUndoManager());
        rebuildAllUI();
    }
}

void MainWindow::onDuplicateTrack()
{
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackList.getNumChildren() <= 0) return;

    int last = trackList.getNumChildren() - 1;
    auto source = trackList.getChild(last);
    auto copy = source.createCopy();
    trackList.addChild(copy, -1, &model.getUndoManager());
    engine.getMainProcessor()->rebuildRoutingGraph();
    rebuildAllUI();
}

void MainWindow::onToggleBrowserPanel()
{
    if (browserPanel == nullptr) return;
    browserPanel->setVisible(!browserPanel->isVisible());
}

void MainWindow::onImportAudio()
{
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import Audio",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "Audio Files (*.wav *.aiff *.aif *.mp3 *.flac *.ogg)");
    if (path.isEmpty()) return;

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0)
        onAddTrack();

    trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0) return;

    QStringList trackNames;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto name = QString::fromUtf8(
            trackList.getChild(i).getProperty(IDs::name).toString().toRawUTF8());
        trackNames << QString("Track %1: %2").arg(i + 1).arg(name);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(this, "Select Track",
        "Import to which track?", trackNames, 0, false, &ok);
    if (!ok || selected.isEmpty()) return;

    int trackIndex = trackNames.indexOf(selected);
    if (trackIndex < 0) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &engine.getProjectModel().getUndoManager());
    }

    QFileInfo fi(path);
    double duration = 4.0;
    auto& pool = engine.getProjectPool();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        pool.getFormatManager().createReaderFor(juce::File(path.toUtf8().constData())));
    if (reader != nullptr)
    {
        duration = reader->lengthInSamples / reader->sampleRate;
    }

    double startTime = 0.0;
    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        auto c = clipList.getChild(i);
        double end = static_cast<double>(c.getProperty(IDs::startTime))
                   + static_cast<double>(c.getProperty(IDs::duration));
        startTime = (std::max)(startTime, end);
    }

    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              startTime, duration,
                                              path.toUtf8().constData());
    clipList.addChild(clip, -1, &engine.getProjectModel().getUndoManager());

    engine.getMainProcessor()->rebuildRoutingGraph();
}

void MainWindow::onImportMIDI()
{
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import MIDI",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "MIDI Files (*.mid *.midi)");
    if (path.isEmpty()) return;

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0)
        onAddTrack();

    trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0) return;

    juce::File midiFile(path.toUtf8().constData());
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
    {
        QMessageBox::warning(this, "Error", "Could not open MIDI file.");
        return;
    }

    juce::MidiFile midiData;
    if (!midiData.readFrom(stream))
    {
        QMessageBox::warning(this, "Error", "Failed to read MIDI file.");
        return;
    }

    int midiTimeFormat = static_cast<int>(midiData.getTimeFormat());
    if (midiTimeFormat <= 0)
    {
        QMessageBox::warning(this, "Error", "SMPTE timecode MIDI files are not supported.");
        return;
    }
    int midiTicksPerQuarterNote = midiTimeFormat;
    double bpm = 120.0;

    // Read tempo from MIDI file's first track (usually tempo track)
    if (midiData.getNumTracks() > 0)
    {
        auto* tempoTrack = midiData.getTrack(0);
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e)
        {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTempoMetaEvent())
            {
                double secPerQuarter = ev->message.getTempoSecondsPerQuarterNote();
                if (secPerQuarter > 0.0)
                    bpm = 60.0 / secPerQuarter;
                break;
            }
        }
    }

    double secondsPerTick = (60.0 / bpm) / static_cast<double>(midiTicksPerQuarterNote);

    // Ask which track to import to
    QStringList trackNames;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto name = QString::fromUtf8(
            trackList.getChild(i).getProperty(IDs::name).toString().toRawUTF8());
        trackNames << QString("Track %1: %2").arg(i + 1).arg(name);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(this, "Select Track",
        "Import to which track?", trackNames, 0, false, &ok);
    if (!ok || selected.isEmpty()) return;

    int trackIndex = trackNames.indexOf(selected);
    if (trackIndex < 0) return;

    // Import each MIDI track as a separate clip
    for (int mt = 0; mt < midiData.getNumTracks(); ++mt)
    {
        auto* midiTrack = midiData.getTrack(mt);
        if (midiTrack == nullptr || midiTrack->getNumEvents() == 0)
            continue;

        // Calculate clip duration from last event
        double clipDuration = 4.0;
        auto* lastEventHolder = midiTrack->getEventPointer(midiTrack->getNumEvents() - 1);
        if (lastEventHolder != nullptr)
            clipDuration = lastEventHolder->message.getTimeStamp() * secondsPerTick + 1.0;

        auto trackTree = trackList.getChild(trackIndex);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
        {
            clipList = juce::ValueTree(IDs::CLIP_LIST);
            trackTree.addChild(clipList, -1, &engine.getProjectModel().getUndoManager());
        }

        double clipStartTime = 0.0;
        for (int i = 0; i < clipList.getNumChildren(); ++i)
        {
            auto c = clipList.getChild(i);
            double end = static_cast<double>(c.getProperty(IDs::startTime))
                       + static_cast<double>(c.getProperty(IDs::duration));
            clipStartTime = (std::max)(clipStartTime, end);
        }

        auto clip = ProjectModel::createMidiClipEmpty(
            ("MIDI Track " + juce::String(mt + 1)).toRawUTF8(),
            clipStartTime, clipDuration);
        auto midiNotes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);

        for (int e = 0; e < midiTrack->getNumEvents(); ++e)
        {
            auto* eventHolder = midiTrack->getEventPointer(e);
            if (eventHolder == nullptr) continue;

            auto& msg = eventHolder->message;
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                double tickTime = msg.getTimeStamp();
                double beatTime = tickTime / static_cast<double>(midiTicksPerQuarterNote);

                double noteDurBeats = 0.25;
                int noteNum = msg.getNoteNumber();
                for (int e2 = e + 1; e2 < midiTrack->getNumEvents(); ++e2)
                {
                    auto* ev2 = midiTrack->getEventPointer(e2);
                    if (ev2 != nullptr && ev2->message.isNoteOff() &&
                        ev2->message.getNoteNumber() == noteNum)
                    {
                        double offTick = ev2->message.getTimeStamp();
                        noteDurBeats = (offTick - tickTime) / static_cast<double>(midiTicksPerQuarterNote);
                        break;
                    }
                }

                midiNotes.addChild(ProjectModel::createMidiNote(
                    noteNum, static_cast<float>(msg.getVelocity()) / 127.0f,
                    beatTime, noteDurBeats), -1, nullptr);
            }
        }

        if (midiNotes.getNumChildren() > 0)
        {
            clipList.addChild(clip, -1, &engine.getProjectModel().getUndoManager());
        }
    }

    engine.getMainProcessor()->rebuildRoutingGraph();
}

void MainWindow::onBPMChanged(double bpm)
{
    auto& model = engine.getProjectModel();
    model.getTree().setProperty(IDs::tempo, bpm, &model.getUndoManager());
}

void MainWindow::onMetronomeToggled(bool enabled)
{
    auto& model = engine.getProjectModel();
    auto transport = model.getTransportTree();
    transport.setProperty(IDs::metronomeEnabled, enabled, &model.getUndoManager());
    Q_UNUSED(enabled);
}

void MainWindow::onRecordToggle()
{
    auto* proc = engine.getMainProcessor();
    if (proc->isRecording())
    {
        proc->stopRecording();
        engine.getTransportManager().setPlaying(false);
        statusBar()->showMessage("Recording stopped", 3000);
    }
    else
    {
        if (proc->startRecording())
        {
            engine.getTransportManager().setPlaying(true);
            statusBar()->showMessage("Recording...", 0);
        }
    }
}

void MainWindow::pumpJuceMessages()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
}
