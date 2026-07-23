#include "MainWindow.h"
#include "../common/DebugLog.h"
#include "TimelineView.h"
#include "AboutDialog.h"
#include "PhraseGeneratorDialog.h"
#include "PluginScannerDialog.h"
#include "PreferencesDialog.h"
#include "../engine/AudioEngine.h"
#include <QMenuBar>
#include <QSettings>
#include <QApplication>

void MainWindow::setupMenuBar()
{
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

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    undoAction = editMenu->addAction(tr("&Undo"), this, &MainWindow::onUndo);
    undoAction->setShortcut(QKeySequence::Undo);

    redoAction = editMenu->addAction(tr("&Redo"), this, &MainWindow::onRedo);
    redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));

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

    auto* trackMenu = menuBar()->addMenu(tr("&Track"));

    auto* addTrackAction = trackMenu->addAction(tr("&Add Track"), this, &MainWindow::onAddTrack);
    addTrackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    auto* deleteTrackAction = trackMenu->addAction(tr("&Delete Track"), this, &MainWindow::onDeleteTrack);
    deleteTrackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));

    auto* renameTrackAction = trackMenu->addAction(tr("&Rename Track"), this, &MainWindow::onRenameTrack);
    renameTrackAction->setShortcut(QKeySequence(Qt::Key_F2));

    auto* dupTrackAction = trackMenu->addAction(tr("&Duplicate Track"), this, &MainWindow::onDuplicateTrack);

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
