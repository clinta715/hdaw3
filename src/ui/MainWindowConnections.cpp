#include "MainWindow.h"
#include "../engine/AudioEngine.h"
#include "TimelineView.h"
#include "TimelineScene.h"
#include "ClipItem.h"
#include "MixerWidget.h"
#include "PianoRollWidget.h"
#include "FXChainWidget.h"
#include "AutomationLaneWidget.h"
#include "AudioClipEditorWidget.h"
#include "StepEditorWidget.h"
#include "ModulationWidget.h"
#include "StatusBar.h"
#include "ProjectPoolBrowser.h"
#include "PreferencesDialog.h"
#include "../engine/MainAudioProcessor.h"

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
            bottomStack->setCurrentIndex(BottomPanel::Automation);
        });

    connect(timelineView, &TimelineView::trackSelectionChanged, this,
        [this](int trackIndex) {
            selectedTrack = trackIndex;
            timelineView->setSelectedTrack(trackIndex);
            int idx = bottomStack->currentIndex();
            if (idx == BottomPanel::FxChain && selectedTrack >= 0)
                fxChainWidget->loadTrack(trackIndex);
            if (idx == BottomPanel::Automation && selectedTrack >= 0)
                automationWidget->loadTrack(trackIndex);
            if (idx == BottomPanel::Modulation && selectedTrack >= 0)
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

    connect(timelineView, &TimelineView::fileImported, browserPanel, &ProjectPoolBrowser::addToPool);

    connect(&timecodeTimer, &QTimer::timeout, this, &MainWindow::updateTimecode);
    timecodeTimer.start(33);

    connect(&jucePumpTimer, &QTimer::timeout, this, &MainWindow::pumpJuceMessages);
    jucePumpTimer.start(10);

    auto midiDevices = engine.getMidiService().getAvailableDevices();
    QStringList devList;
    for (const auto& d : midiDevices)
        devList << QString::fromStdString(d);
    timelineView->getToolbar()->populateMidiDevices(devList);
    QString lastMidi = PreferencesDialog::settings().value(PreferencesDialog::kKeyMidiDevice).toString();
    if (!lastMidi.isEmpty() && devList.contains(lastMidi))
        timelineView->getToolbar()->selectMidiDevice(lastMidi);

    auto& s = PreferencesDialog::settings();
    int countInBars = s.value(PreferencesDialog::kKeyCountInBars, 1).toInt();
    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
        mainProc->setCountInEnabled(countInBars > 0, countInBars);
    timelineView->getToolbar()->setCountInEnabled(countInBars > 0);
}

void MainWindow::connectBottomPanelSignals()
{
    connect(pianoRollWidget, &PianoRollWidget::clipClosed, this,
        [this]() { pianoRollWidget->clear(); bottomStack->setCurrentIndex(BottomPanel::Mixer); });

    connect(audioEditorWidget, &AudioClipEditorWidget::clipClosed, this,
        [this]() { audioEditorWidget->clear(); bottomStack->setCurrentIndex(BottomPanel::Mixer); });

    connect(stepEditorWidget, &StepEditorWidget::clipClosed, this,
        [this]() { stepEditorWidget->clear(); bottomStack->setCurrentIndex(BottomPanel::Mixer); });

    connect(stepEditorWidget, &StepEditorWidget::switchToPianoRoll, this,
        [this]() {
            if (stepEditorWidget->hasClip())
            {
                pianoRollWidget->loadClip(stepEditorWidget->getClipTree());
                bottomStack->setCurrentIndex(BottomPanel::PianoRoll);
            }
        });

    connect(mixerWidget, &MixerWidget::fxButtonClicked, this,
        [this](int trackIndex) {
            selectedTrack = trackIndex;
            fxChainWidget->loadTrack(trackIndex);
            bottomStack->setCurrentIndex(BottomPanel::FxChain);
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
