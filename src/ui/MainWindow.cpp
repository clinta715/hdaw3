#include "MainWindow.h"
#include "TimelineView.h"
#include "MixerWidget.h"
#include "PianoRollWidget.h"
#include "FXChainWidget.h"
#include "AutomationLaneWidget.h"
#include "AudioClipEditorWidget.h"
#include "StepEditorWidget.h"
#include "ModulationWidget.h"
#include "StatusBar.h"
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
#include "../engine/AudioImport.h"
#include "../engine/MidiImport.h"
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
    // PluginManager wiring moved to AudioEngine::initialize()    mcp::registerAllTools(*mcpServer_);

    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();

    setupLayout();
    setupMenuBar();

    // Load saved preferences
    double clipDur = PreferencesDialog::getDefaultClipDuration();
    timelineView->getToolbar()->setDefaultClipLen(clipDur);
    timelineView->getInteraction()->setDefaultClipDuration(clipDur);

    auto* toolbar = timelineView->getToolbar();
    toolbar->addTrackPluginMenu(nullptr, engine.getPluginService());

    toolbar->setSnap(PreferencesDialog::getSnapEnabled());
    toolbar->setSnapDivision(PreferencesDialog::getSnapDivision());

    // Listen on the root project tree (not the TRANSPORT child) so the
    // listener survives project rebuilds. Child-tree listeners become
    // orphaned when the tree is cleared (e.g. on File→New).
    engine.getProjectModel().getTree().addListener(this);

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

    engine.getProjectModel().getTree().removeListener(this);
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
        auto* tlb = timelineView->getToolbar();
        if (tlb != nullptr)
        {
            PreferencesDialog::setSnapEnabled(tlb->isSnapEnabled());
            PreferencesDialog::setSnapDivision(tlb->getSnapDivisionIndex());
        }
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

    auto* zoomFitAllAction = viewMenu->addAction(tr("Zoom to Fit &All"));
    zoomFitAllAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(zoomFitAllAction, &QAction::triggered, timelineView, &TimelineView::zoomToFitAll);

    auto* zoomFitSelAction = viewMenu->addAction(tr("Zoom to Fit &Selection"));
    zoomFitSelAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    connect(zoomFitSelAction, &QAction::triggered, timelineView, &TimelineView::zoomToFitSelection);

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
        bool noMcp = qApp->property("hdaw.noMcp").toBool();
        if (noMcp) {
            mcpHttpAction->setChecked(false);
            mcpHttpAction->setEnabled(false);
        } else {
            mcpHttpAction->setChecked(settings.value("mcp/httpEnabled", false).toBool());
        }
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
        PreferencesDialog dialog(&engine, this);
        if (dialog.exec() == QDialog::Accepted)
        {
            double clipDur = PreferencesDialog::getDefaultClipDuration();
            timelineView->getToolbar()->setDefaultClipLen(clipDur);
            timelineView->getInteraction()->setDefaultClipDuration(clipDur);
            auto& s = PreferencesDialog::settings();
            int countInBars = s.value(PreferencesDialog::kKeyCountInBars, 1).toInt();
            if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
                mainProc->setCountInEnabled(countInBars > 0, countInBars);
        }
    });
}

void MainWindow::startMcpHttpServer()
{
    if (mcpHttp_ != nullptr) return;
    auto& settings = PreferencesDialog::settings();
    quint16 port = static_cast<quint16>(settings.value("mcp/httpPort", 8765).toInt());
    QString host = settings.value("mcp/httpHost", "127.0.0.1").toString();
    mcpHttp_ = new mcp::TransportHttp(port, host);
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
    // Build a custom status bar widget with permanent fields (BPM, time
    // signature, sample rate, selected track, MIDI device, etc.). The
    // built-in QStatusBar is kept around for transient message overlays
    // via statusBar()->showMessage().
    auto* qsb = new QStatusBar(this);
    setStatusBar(qsb);
    statusBarWidget = new StatusBar(engine, this);
    qsb->addWidget(statusBarWidget, 1);

    // Keep a QLabel for transient messages alongside the permanent widget.
    auto* transientLabel = new QLabel(this);
    transientLabel->setStyleSheet("color: #c8c8cc; padding: 1px 6px;");
    qsb->addPermanentWidget(transientLabel);
    qsb->showMessage("Ready");

    timelineView = new TimelineView(engine, this);
    browserPanel = new ProjectPoolBrowser(engine, this);

    setupBottomPanel();

    mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
    mainVerticalSplitter->addWidget(timelineView);
    mainVerticalSplitter->addWidget(bottomStack->parentWidget());
    mainVerticalSplitter->setStretchFactor(0, 3);
    mainVerticalSplitter->setStretchFactor(1, 1);

    mainHorizontalSplitter = new QSplitter(Qt::Horizontal, this);
    mainHorizontalSplitter->addWidget(mainVerticalSplitter);
    mainHorizontalSplitter->addWidget(browserPanel);
    mainHorizontalSplitter->setStretchFactor(0, 4);
    mainHorizontalSplitter->setStretchFactor(1, 1);

    setCentralWidget(mainHorizontalSplitter);

    connectTimelineSignals();
    connectBottomPanelSignals();
    restoreWindowGeometry();
}

void MainWindow::setupBottomPanel()
{
    auto* bottomContainer = new QFrame(this);
    bottomContainer->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    auto* bottomLayout = new QVBoxLayout(bottomContainer);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

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
    auto* modTab = makeTab("Modulation", 6);
    juce::ignoreUnused(mixerTab, pianoTab, fxTab, autoTab, audioTab, stepTab, modTab);

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

    modulationWidget = new ModulationWidget(engine, bottomStack);
    bottomStack->addWidget(modulationWidget);

    connect(tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 2 && selectedTrack >= 0)
            fxChainWidget->loadTrack(selectedTrack);
        if (id == 3 && selectedTrack >= 0)
            automationWidget->loadTrack(selectedTrack);
        if (id == 6 && selectedTrack >= 0)
            modulationWidget->loadTrack(selectedTrack);
        bottomStack->setCurrentIndex(id);
    });

    connect(bottomStack, &QStackedWidget::currentChanged, this, [this](int index) {
        if (index < 0 || index >= static_cast<int>(tabButtons.size())) return;
        for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            tabButtons[i]->setChecked(i == index);
    });
    if (bottomStack->currentIndex() >= 0 && bottomStack->currentIndex() < static_cast<int>(tabButtons.size()))
        tabButtons[bottomStack->currentIndex()]->setChecked(true);

    bottomStack->setCurrentIndex(0);
    bottomLayout->addWidget(bottomStack);
}

void MainWindow::connectTimelineSignals()
{
    auto* scene = timelineView->getScene();

    connect(scene, &TimelineScene::clipSelected, this, &MainWindow::onClipSelected);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this, scene]() {
        int count = 0;
        for (auto* item : scene->selectedItems())
            if (dynamic_cast<ClipItem*>(item) != nullptr) ++count;
        if (statusBarWidget)
            statusBarWidget->setSelectionCount(count);
        if (count == 0)
            statusBar()->clearMessage();
        else if (count == 1)
            statusBar()->showMessage("1 clip selected", 0);
        else
            statusBar()->showMessage(QString("%1 clips selected").arg(count), 0);
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
            timelineView->setSelectedTrack(trackIndex);
            int idx = bottomStack->currentIndex();
            if (idx == 2 && selectedTrack >= 0)
                fxChainWidget->loadTrack(trackIndex);
            if (idx == 3 && selectedTrack >= 0)
                automationWidget->loadTrack(trackIndex);
            if (idx == 6 && selectedTrack >= 0)
                modulationWidget->loadTrack(trackIndex);
            if (statusBarWidget)
            {
                QString name;
                if (trackIndex >= 0 && trackIndex < readModel->getTrackCount())
                    name = QString::fromStdString(readModel->getTrack(trackIndex).name);
                statusBarWidget->setSelectedTrack(trackIndex, name);
            }
        });

    connect(timelineView, &TimelineView::addTrackClicked, this, &MainWindow::onAddTrack);
    connect(timelineView, &TimelineView::addTrackWithFX, this, &MainWindow::onAddTrackWithFX);
    connect(timelineView, &TimelineView::addTrackWithPlugin, this, &MainWindow::onAddTrackWithPlugin);
    connect(timelineView, &TimelineView::bpmChanged, this, &MainWindow::onBPMChanged);
    connect(timelineView, &TimelineView::metronomeToggled, this, &MainWindow::onMetronomeToggled);
    connect(timelineView, &TimelineView::countInToggled, this, &MainWindow::onCountInToggled);
    connect(timelineView, &TimelineView::timeSigChanged, this, &MainWindow::onTimeSigChanged);
    connect(timelineView, &TimelineView::inputMonitoringChanged, this, &MainWindow::onInputMonitoringChanged);
    connect(timelineView, &TimelineView::midiDeviceChanged, this, &MainWindow::onMidiDeviceChanged);
    connect(timelineView, &TimelineView::recordToggled, this, &MainWindow::onRecordToggle);
    connect(timelineView, &TimelineView::ccRecordToggled, this, &MainWindow::onCcRecordToggled);
    connect(timelineView, &TimelineView::playToggled, this, &MainWindow::onPlayToggle);
    connect(timelineView, &TimelineView::stopRequested, this, &MainWindow::onStop);
    connect(timelineView, &TimelineView::rewindRequested, this, &MainWindow::onRewind);

    connect(&timecodeTimer, &QTimer::timeout, this, &MainWindow::updateTimecode);
    timecodeTimer.start(33);

    connect(&jucePumpTimer, &QTimer::timeout, this, &MainWindow::pumpJuceMessages);
    jucePumpTimer.start(10);

    // Populate MIDI devices and restore last selection
    auto midiDevices = engine.getMidiService().getAvailableDevices();
    QStringList devList;
    for (const auto& d : midiDevices)
        devList << QString::fromStdString(d);
    timelineView->getToolbar()->populateMidiDevices(devList);
    QString lastMidi = PreferencesDialog::settings().value(PreferencesDialog::kKeyMidiDevice).toString();
    if (!lastMidi.isEmpty() && devList.contains(lastMidi))
        timelineView->getToolbar()->selectMidiDevice(lastMidi);

    // Restore count-in from preferences
    auto& s = PreferencesDialog::settings();
    int countInBars = s.value(PreferencesDialog::kKeyCountInBars, 1).toInt();
    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
        mainProc->setCountInEnabled(countInBars > 0, countInBars);
    timelineView->getToolbar()->setCountInEnabled(countInBars > 0);
}

void MainWindow::connectBottomPanelSignals()
{
    connect(pianoRollWidget, &PianoRollWidget::clipClosed, this,
        [this]() { pianoRollWidget->clear(); bottomStack->setCurrentIndex(0); });

    connect(audioEditorWidget, &AudioClipEditorWidget::clipClosed, this,
        [this]() { audioEditorWidget->clear(); bottomStack->setCurrentIndex(0); });

    connect(stepEditorWidget, &StepEditorWidget::clipClosed, this,
        [this]() { stepEditorWidget->clear(); bottomStack->setCurrentIndex(0); });

    connect(stepEditorWidget, &StepEditorWidget::switchToPianoRoll, this,
        [this]() {
            if (stepEditorWidget->hasClip())
            {
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

    connect(fxChainWidget, &FXChainWidget::chainChanged, this,
        [this]() {
            int numTracks = readModel->getTrackCount();
            for (int i = 0; i < numTracks; ++i)
                audioGraphCmds->rebuildTrackFX(i);
        });

    connect(automationWidget, &AutomationLaneWidget::automationChanged, this,
        [this]() {
            int track = automationWidget->currentTrackIndex();
            if (track >= 0)
                audioGraphCmds->rebuildAutomationCache(track);
        });
}

void MainWindow::restoreWindowGeometry()
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

void MainWindow::onClipSelected(const juce::ValueTree& clipTree)
{
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
}

void MainWindow::rebuildAllUI()
{
    timelineView->getScene()->rebuildFromValueTree();
    mixerWidget->rebuild();
    mixerWidget->updateMasterMeter();
    fxChainWidget->clear();
    pianoRollWidget->clear();
    audioEditorWidget->clear();
    automationWidget->clear();

    int numTracks = readModel->getTrackCount();

    // Ensure all track FX are built in the engine
    for (int i = 0; i < numTracks; ++i)
        audioGraphCmds->rebuildTrackFX(i);

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
    audioGraphCmds->rebuildRoutingGraph();
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
    audioGraphCmds->rebuildRoutingGraph();
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
    if (projectCmds->canUndo())
    {
        projectCmds->undo();
        rebuildAllUI();
    }
}

void MainWindow::onRedo()
{
    if (projectCmds->canRedo())
    {
        projectCmds->redo();
        rebuildAllUI();
    }
}

void MainWindow::onPlayToggle()
{
    if (readModel->getTransport().isPlaying)
    {
        transportCmds->stop();
        statusBar()->showMessage("Stopped", 3000);
    }
    else
    {
        transportCmds->play();
        statusBar()->showMessage("Playing", 3000);
    }
}

void MainWindow::onStop()
{
    if (transportCmds->isRecording())
        transportCmds->stopRecording();
    transportCmds->stop();
    statusBar()->showMessage("Stopped", 3000);
}

void MainWindow::onRewind()
{
    transportCmds->rewind();
}

void MainWindow::onLoopToggle()
{
    bool current = readModel->getTransport().isLooping;
    bool next = !current;
    projectCmds->setLooping(next);

    if (loopAction)
    {
        loopAction->blockSignals(true);
        loopAction->setChecked(next);
        loopAction->blockSignals(false);
    }
}

void MainWindow::updateTimecode()
{
    auto transport = readModel->getTransport();
    double seconds = transport.currentTimeSeconds;
    double bpm = transport.bpm;

    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int millis = static_cast<int>((seconds - static_cast<double>(static_cast<int>(seconds))) * 1000.0);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%03d", mins, secs, millis);

    timelineView->getToolbar()->setTimecode(QString::fromUtf8(buf));
    timelineView->getToolbar()->setPlaying(transport.isPlaying);
    timelineView->getToolbar()->setBPM(bpm);

    timelineView->scrollToPlayhead();

    // Update playhead in all open editors
    if (automationWidget)
        automationWidget->setPlayheadPosition(seconds);
    if (audioEditorWidget)
        audioEditorWidget->updatePlayhead(seconds);
    if (pianoRollWidget)
        pianoRollWidget->setPlayheadPosition(seconds, bpm);

    // Refresh the permanent status bar fields
    if (statusBarWidget)
    {
        statusBarWidget->setBPM(bpm);
    }
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

void MainWindow::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    juce::ignoreUnused(parentTree);
    // When a new TRANSPORT node is added (e.g. after File→New or project load),
    // sync the UI to the initial transport state. Properties on the new child
    // are set before addChild, so valueTreePropertyChanged never fires for them.
    if (childWhichHasBeenAdded.hasType(IDs::TRANSPORT))
    {
        bool looping = childWhichHasBeenAdded.getProperty(IDs::isLooping);
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
    int before = readModel->getTrackCount();
    HDAW_LOG("MWAddTrk", QString("before count=%1").arg(before));

    int trackNum = before + 1;
    projectCmds->addTrack("Track " + std::to_string(trackNum));

    int after = readModel->getTrackCount();
    HDAW_LOG("MWAddTrk", QString("after count=%1 (expected %2)")
        .arg(after).arg(before + 1));

    audioGraphCmds->rebuildRoutingGraph();
    rebuildAllUI();
    HDAW_LOG("MWAddTrk", "EXIT");
}

void MainWindow::onAddTrackWithFX(const juce::String& fxType)
{
    onAddTrack();

    int last = readModel->getTrackCount() - 1;
    if (last < 0) return;

    engine.getProjectModel().addFxSlot(last, fxType.toStdString());

    audioGraphCmds->rebuildTrackFX(last);
    rebuildAllUI();
}

void MainWindow::onAddTrackWithPlugin(const juce::String& pluginID, const juce::String& /*pluginFormat*/)
{
    onAddTrack();

    int last = readModel->getTrackCount() - 1;
    if (last < 0) return;

    engine.getProjectModel().addFxSlot(last, "plugin", -1, pluginID.toStdString());

    audioGraphCmds->rebuildTrackFX(last);
    rebuildAllUI();
}

void MainWindow::onDeleteTrack()
{
    int numTracks = readModel->getTrackCount();
    if (numTracks <= 0) return;

    projectCmds->removeTrack(numTracks - 1);
    audioGraphCmds->rebuildRoutingGraph();
    rebuildAllUI();
}

void MainWindow::onRenameTrack()
{
    int numTracks = readModel->getTrackCount();
    if (numTracks <= 0) return;

    int last = numTracks - 1;
    auto snap = readModel->getTrack(last);
    QString current = QString::fromStdString(snap.name);
    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename Track", "Track name:",
        QLineEdit::Normal, current, &ok);
    if (ok && !newName.isEmpty())
    {
        projectCmds->setTrackName(last, newName.toStdString());
        rebuildAllUI();
    }
}

void MainWindow::onDuplicateTrack()
{
    int numTracks = readModel->getTrackCount();
    if (numTracks <= 0) return;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    int last = numTracks - 1;
    auto source = trackList.getChild(last);
    auto copy = source.createCopy();
    trackList.addChild(copy, -1, &model.getUndoManager());
    audioGraphCmds->rebuildRoutingGraph();
    rebuildAllUI();
}

void MainWindow::onToggleBrowserPanel()
{
    if (browserPanel == nullptr) return;
    browserPanel->setVisible(!browserPanel->isVisible());
}

int MainWindow::promptForImportTrack(QWidget* parent, AudioEngine& eng, const QString& title)
{
    auto trackList = eng.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0)
        return -1;

    QStringList trackNames;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto name = QString::fromUtf8(
            trackList.getChild(i).getProperty(IDs::name).toString().toRawUTF8());
        trackNames << QString("Track %1: %2").arg(i + 1).arg(name);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(parent, title,
        "Import to which track?", trackNames, 0, false, &ok);
    if (!ok || selected.isEmpty()) return -1;
    return trackNames.indexOf(selected);
}

void MainWindow::onImportAudio()
{
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import Audio",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "Audio Files (*.wav *.aiff *.aif *.mp3 *.flac *.ogg)");
    if (path.isEmpty()) return;

    if (readModel->getTrackCount() == 0)
        onAddTrack();

    int trackIdx = promptForImportTrack(this, engine, "Import Audio");
    if (trackIdx < 0) return;

    if (HDAW::importAudioFile(engine, path, trackIdx))
        rebuildAllUI();
}

void MainWindow::onImportMIDI()
{
    auto& settings = PreferencesDialog::settings();
    auto path = QFileDialog::getOpenFileName(this, "Import MIDI",
        settings.value(PreferencesDialog::kKeyLastProjectDir).toString(),
        "MIDI Files (*.mid *.midi)");
    if (path.isEmpty()) return;

    if (readModel->getTrackCount() == 0)
        onAddTrack();

    int trackIdx = promptForImportTrack(this, engine, "Import MIDI");
    if (trackIdx < 0) return;

    if (HDAW::importMidiFile(engine, path, trackIdx))
        rebuildAllUI();
}

void MainWindow::onBPMChanged(double bpm)
{
    projectCmds->setTempo(bpm);
}

void MainWindow::onMetronomeToggled(bool enabled)
{
    projectCmds->setMetronomeEnabled(enabled);
}

void MainWindow::onCountInToggled(bool enabled)
{
    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
        mainProc->setCountInEnabled(enabled, 1);
}

void MainWindow::onTimeSigChanged(int numerator, int denominator)
{
    auto& model = engine.getProjectModel();
    auto transport = model.getTransportTree();
    transport.setProperty(IDs::timeSigNumerator, numerator, &model.getUndoManager());
    transport.setProperty(IDs::timeSigDenominator, denominator, &model.getUndoManager());
    if (statusBarWidget)
        statusBarWidget->setTimeSignature(numerator, denominator);
}

void MainWindow::onInputMonitoringChanged(int trackIndex, bool enabled)
{
    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
    {
        if (auto* rm = mainProc->getRoutingManager())
            rm->setInputMonitoring(trackIndex, enabled);
    }
}

void MainWindow::onMidiDeviceChanged(const QString& deviceIdentifier)
{
    if (deviceIdentifier.isEmpty())
        engine.getMidiService().closeDevice();
    else
        engine.getMidiService().openDevice(deviceIdentifier.toStdString());
    if (statusBarWidget)
        statusBarWidget->setMidiDevice(deviceIdentifier);
    PreferencesDialog::settings().setValue(PreferencesDialog::kKeyMidiDevice, deviceIdentifier);
}

void MainWindow::onRecordToggle()
{
    if (transportCmds->isRecording())
    {
        transportCmds->stopRecording();
        transportCmds->stop();
        if (statusBarWidget) statusBarWidget->setRecording(false);
        statusBar()->showMessage("Recording stopped", 3000);
    }
    else
    {
        transportCmds->startRecording();
        transportCmds->play();
        if (statusBarWidget) statusBarWidget->setRecording(true);
        statusBar()->showMessage("Recording...", 0);
    }
}

void MainWindow::onCcRecordToggled(bool armed)
{
    engine.setMidiCcRecordArmed(armed);
    if (armed)
    {
        // Set the callback to capture CC events into the currently
        // selected track's clip at the current time. Routed through
        // the audio engine's MIDI input callback.
        engine.setMidiCcCallback([this](int controller, int value) {
            if (selectedTrack < 0) return;
            if (selectedTrack >= readModel->getTrackCount()) return;
            auto trackList = engine.getProjectModel().getTrackListTree();
            auto trackTree = trackList.getChild(selectedTrack);
            auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid()) return;

            auto transport = readModel->getTransport();
            double currentTime = transport.currentTimeSeconds;

            // Find the clip at the current time on the selected track.
            for (int i = 0; i < clipList.getNumChildren(); ++i)
            {
                auto clip = clipList.getChild(i);
                double clipStart = clip.getProperty(IDs::startTime);
                double clipDur = clip.getProperty(IDs::duration);
                if (currentTime >= clipStart && currentTime < clipStart + clipDur)
                {
                    auto ccList = clip.getChildWithName(IDs::CC_LIST);
                    if (!ccList.isValid())
                    {
                        ccList = juce::ValueTree(IDs::CC_LIST);
                        clip.addChild(ccList, -1, nullptr);
                    }
                    double bpm = transport.bpm;
                    if (bpm <= 0) bpm = 120.0;
                    double currentBeat = currentTime * bpm / 60.0;

                    juce::ValueTree pt(IDs::CC_POINT);
                    pt.setProperty(IDs::controllerNumber, controller,
                                   &engine.getProjectModel().getUndoManager());
                    pt.setProperty(IDs::beat, currentBeat,
                                   &engine.getProjectModel().getUndoManager());
                    pt.setProperty(IDs::value, value,
                                   &engine.getProjectModel().getUndoManager());
                    ccList.addChild(pt, -1, &engine.getProjectModel().getUndoManager());
                    break;
                }
            }
        });
        statusBar()->showMessage("CC record armed — move controllers during playback", 0);
    }
    else
    {
        engine.setMidiCcCallback(nullptr);
        statusBar()->showMessage("CC record off", 2000);
    }
}

void MainWindow::pumpJuceMessages()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
}
