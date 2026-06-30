#include "TimelineView.h"
#include "Theme.h"
#include <QScrollBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QGraphicsView>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QKeyEvent>
#include <QInputDialog>
#include <QApplication>
#include <QShowEvent>
#include "DebugLog.h"

TimelineView::TimelineView(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    setupUI();
    connectSignals();
}

TimelineView::~TimelineView()
{
    playheadCursor->stopSync();
}

void TimelineView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // The QGraphicsView computes its vertical scroll position based on
    // viewport size during the first layout pass, which finishes after
    // setupUI returns. Calling setValue(0) here, after the layout is
    // fully resolved and the widget is shown, pins the bar to the top.
    // syncRulerWithScene then mirrors that to the track headers.
    if (graphicsView != nullptr)
    {
        graphicsView->verticalScrollBar()->setValue(0);
        graphicsView->horizontalScrollBar()->setValue(0);
        syncRulerWithScene();
    }
}

void TimelineView::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Toolbar
    toolbar = new TimelineToolbar(this);
    mainLayout->addWidget(toolbar);

    // Content area: track headers + graphics view
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Track headers
    trackHeaders = new TrackHeaderWidget(engine, this);
    contentLayout->addWidget(trackHeaders, 0, Qt::AlignLeft);

    // Graphics view
    graphicsView = new QGraphicsView(this);
    // Anchor the scene to the top-left of the viewport so its y=0 aligns with
    // the track headers' y=0. The QGraphicsView default is Qt::AlignCenter,
    // which offsets the scene vertically when it fits the viewport, misaligning
    // clip rows with their headers.
    graphicsView->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    graphicsView->setRenderHint(QPainter::Antialiasing, true);
    graphicsView->setDragMode(QGraphicsView::NoDrag);
    graphicsView->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    graphicsView->setBackgroundBrush(ThemeColors::bgWindow());
    graphicsView->setFrameStyle(QFrame::NoFrame);
    graphicsView->setAcceptDrops(true);
    graphicsView->viewport()->installEventFilter(this);

    graphicsView->viewport()->setFocusPolicy(Qt::StrongFocus);
    graphicsView->setFocusPolicy(Qt::StrongFocus);

    contentLayout->addWidget(graphicsView, 1);
    mainLayout->addLayout(contentLayout, 1);

    // Scene
    timelineScene = new TimelineScene(engine, this);
    graphicsView->setScene(timelineScene);

    // Interaction
    interaction = new TimelineInteraction(timelineScene, engine, this);
    interaction->setUndoManager(&engine.getProjectModel().getUndoManager());
    timelineScene->setInteraction(interaction);

    // Ruler
    rulerItem = new TimeRuler(engine);
    timelineScene->addItem(rulerItem);

    // Playhead
    playheadCursor = new PlayheadCursor(engine.getTransportManager(), pixelsPerSecond);
    timelineScene->addItem(playheadCursor);
    playheadCursor->startSync();

    // Loop markers
    loopStartMarker = new LoopMarker(*rulerItem, LoopMarker::Left);
    loopEndMarker = new LoopMarker(*rulerItem, LoopMarker::Right);
    timelineScene->addItem(loopStartMarker);
    timelineScene->addItem(loopEndMarker);

    // Set loop markers to ruler positions
    loopStartMarker->setPos(
        rulerItem->xFromTime(rulerItem->getLoopStart()), 0);
    loopEndMarker->setPos(
        rulerItem->xFromTime(rulerItem->getLoopEnd()), 0);
}

void TimelineView::connectSignals()
{
    // Toolbar signals
    connect(toolbar, &TimelineToolbar::snapToggleChanged, this, [this](bool en) {
        interaction->setSnapEnabled(en);
    });

    connect(toolbar, &TimelineToolbar::snapDivisionChanged, this, [this](int idx) {
        interaction->setSnapDivision(static_cast<TimelineInteraction::SnapDivision>(idx));
    });

    connect(toolbar, &TimelineToolbar::zoomInClicked, this, &TimelineView::zoomIn);
    connect(toolbar, &TimelineToolbar::zoomOutClicked, this, &TimelineView::zoomOut);

    connect(toolbar, &TimelineToolbar::gridTypeChanged, this, [this](bool showBeats) {
        rulerItem->setShowBeats(showBeats);
        rulerItem->update();
    });

    connect(toolbar, &TimelineToolbar::loopToggleChanged, this, [this](bool en) {
        auto transportTree = engine.getProjectModel().getTransportTree();
        transportTree.setProperty(IDs::isLooping, en, &engine.getProjectModel().getUndoManager());
    });

    connect(toolbar, &TimelineToolbar::addTrackClicked, this, &TimelineView::addTrackClicked);
    connect(toolbar, &TimelineToolbar::addTrackWithFX, this, &TimelineView::addTrackWithFX);
    connect(toolbar, &TimelineToolbar::addTrackWithPlugin, this, &TimelineView::addTrackWithPlugin);
    connect(toolbar, &TimelineToolbar::bpmChanged, this, &TimelineView::bpmChanged);
    connect(toolbar, &TimelineToolbar::metronomeToggled, this, &TimelineView::metronomeToggled);

    connect(this, &TimelineView::defaultClipLenChanged, this, [this](double beats) {
        interaction->setDefaultClipDuration(beats);
    });
    connect(toolbar, &TimelineToolbar::defaultClipLenChanged, this, &TimelineView::defaultClipLenChanged);

    connect(toolbar, &TimelineToolbar::followPlayheadChanged, this, &TimelineView::setFollowPlayhead);
    connect(toolbar, &TimelineToolbar::recordClicked, this, &TimelineView::recordToggled);
    connect(toolbar, &TimelineToolbar::playClicked, this, &TimelineView::playToggled);
    connect(toolbar, &TimelineToolbar::stopClicked, this, &TimelineView::stopRequested);
    connect(toolbar, &TimelineToolbar::rewindClicked, this, &TimelineView::rewindRequested);

    // Ruler seek
    connect(rulerItem, &TimeRuler::seekRequested, this, [this](double time) {
        auto& tm = engine.getTransportManager();
        tm.setCurrentSample(static_cast<int64_t>(time * tm.getSampleRate()));
    });

    connect(rulerItem, &TimeRuler::loopBoundsChanged, this, [this](double start, double end) {
        loopStartMarker->setTime(start);
        loopEndMarker->setTime(end);
        loopStartMarker->setPos(start * pixelsPerSecond, 0);
        loopEndMarker->setPos(end * pixelsPerSecond, 0);
    });

    // Track count changes
    connect(timelineScene, &TimelineScene::trackCountChanged, this, [this](int) {
        trackHeaders->rebuild();
    });

    // Track selection
    connect(trackHeaders, &TrackHeaderWidget::trackSelectionChanged, this,
        &TimelineView::trackSelectionChanged);

    connect(trackHeaders, &TrackHeaderWidget::addTrackRequested, this, &TimelineView::addTrackClicked);
    connect(trackHeaders, &TrackHeaderWidget::addTrackWithFX, this, &TimelineView::addTrackWithFX);
    connect(trackHeaders, &TrackHeaderWidget::addTrackWithPlugin, this, &TimelineView::addTrackWithPlugin);

    // Automation toggle
    connect(trackHeaders, &TrackHeaderWidget::automationToggled, this,
        &TimelineView::automationToggled);

    // Vertical scroll sync
    connect(graphicsView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        syncRulerWithScene();
    });

    connect(graphicsView->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        syncRulerWithScene();
    });
}

void TimelineView::setZoom(double factor)
{
    pixelsPerSecond = std::clamp(pixelsPerSecond * factor, minPPS, maxPPS);

    timelineScene->setPixelsPerSecond(pixelsPerSecond);
    rulerItem->setPixelsPerSecond(pixelsPerSecond);
    playheadCursor->setPixelsPerSecond(pixelsPerSecond);

    // Update loop marker positions
    loopStartMarker->setPos(rulerItem->xFromTime(rulerItem->getLoopStart()), 0);
    loopEndMarker->setPos(rulerItem->xFromTime(rulerItem->getLoopEnd()), 0);

    update();
}

void TimelineView::zoomIn()
{
    setZoom(zoomFactor);
}

void TimelineView::zoomOut()
{
    setZoom(1.0 / zoomFactor);
}

void TimelineView::setFollowPlayhead(bool follow)
{
    playheadCursor->setFollowPlayhead(follow);
}

void TimelineView::selectTrack(int index)
{
    trackHeaders->setSelectedTrack(index);
}

void TimelineView::scrollToPlayhead()
{
    if (!playheadCursor->isFollowing())
        return;

    double playheadX = playheadCursor->pos().x();
    double viewWidth = graphicsView->viewport()->width();
    double centerX = playheadX - viewWidth * 0.3;

    graphicsView->horizontalScrollBar()->setValue(
        static_cast<int>(centerX));
}

void TimelineView::syncRulerWithScene()
{
    double sceneHeight = timelineScene->sceneRect().height();
    double viewportHeight = graphicsView->viewport()->height();
    double maxScrollY = (std::max)(0.0, sceneHeight - viewportHeight);
    double scrollY = std::clamp(
        static_cast<double>(graphicsView->verticalScrollBar()->value()),
        0.0, maxScrollY);

    rulerItem->setY(scrollY);
    playheadCursor->setY(scrollY);
    playheadCursor->setViewRectHeight(sceneHeight);
    loopStartMarker->setY(scrollY);
    loopEndMarker->setY(scrollY);

    trackHeaders->setScrollOffset(scrollY);
}

bool TimelineView::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == graphicsView->viewport())
    {
        if (event->type() == QEvent::ContextMenu)
            HDAW_LOG("TVEvt", QString("ContextMenu event received"));
        if (event->type() == QEvent::DragEnter)
        {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasUrls() || de->mimeData()->hasFormat("text/plain"))
            {
                de->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove)
        {
            auto* dm = static_cast<QDragMoveEvent*>(event);
            dm->acceptProposedAction();
            return true;
        }
        else if (event->type() == QEvent::Drop)
        {
            auto* drop = static_cast<QDropEvent*>(event);
            QString filePath;

            if (drop->mimeData()->hasUrls())
            {
                auto urls = drop->mimeData()->urls();
                if (!urls.isEmpty())
                    filePath = urls.first().toLocalFile();
            }
            else if (drop->mimeData()->hasText())
            {
                filePath = drop->mimeData()->text();
            }

            if (!filePath.isEmpty())
            {
                QPointF scenePos = graphicsView->mapToScene(drop->position().toPoint());
                handleFileDrop(filePath, scenePos);
                drop->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::KeyPress)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace)
            {
                auto selectedItems = timelineScene->selectedItems();
                for (auto* item : selectedItems)
                {
                    auto* clip = dynamic_cast<ClipItem*>(item);
                    if (clip != nullptr)
                    {
                        auto clipTree = clip->getClipTree();
                        auto parentTree = clipTree.getParent();
                        if (parentTree.isValid())
                        {
                            parentTree.removeChild(clipTree, &engine.getProjectModel().getUndoManager());
                        }
                    }
                }
                keyEvent->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::ContextMenu)
        {
            auto* cmEvent = static_cast<QContextMenuEvent*>(event);
            QPointF scenePos = graphicsView->mapToScene(cmEvent->pos());
            QGraphicsItem* item = timelineScene->itemAt(scenePos, QTransform());

            QMenu menu;

            // No parentItem() walk: ClipItem is not grouped under track-lane containers.
            ClipItem* clip = dynamic_cast<ClipItem*>(item);

            if (clip != nullptr)
            {
                auto* deleteAction = menu.addAction("Delete Clip");
                connect(deleteAction, &QAction::triggered, this, [this, clip]() {
                    auto clipTree = clip->getClipTree();
                    auto parentTree = clipTree.getParent();
                    if (parentTree.isValid())
                        parentTree.removeChild(clipTree, &engine.getProjectModel().getUndoManager());
                });

                auto* openAction = menu.addAction("Open in Editor");
                connect(openAction, &QAction::triggered, this, [this, clip]() {
                    emit timelineScene->clipSelected(clip->getClipTree());
                });

                menu.addSeparator();

                auto* splitAction = menu.addAction("Split");
                connect(splitAction, &QAction::triggered, this, [this, cmEvent, clip, scenePos]() {
                    Q_UNUSED(cmEvent);
                    Q_UNUSED(scenePos);
                    // Future: implement split at playhead or click position
                });
            }
            else
            {
                // Right-click on empty timeline space
                double timeSeconds = scenePos.x() / pixelsPerSecond;

                auto* addTrackAction = menu.addAction("Add Track");
                connect(addTrackAction, &QAction::triggered, this, &TimelineView::addTrackClicked);

                menu.addSeparator();

                auto* addTempoAction = menu.addAction("Add Tempo Change Here...");
                connect(addTempoAction, &QAction::triggered, this, [this, timeSeconds]() {
                    bool ok = false;
                    double newBpm = QInputDialog::getDouble(
                        QApplication::activeWindow(), "Tempo Change",
                        QString("BPM at %1s:").arg(timeSeconds, 0, 'f', 2),
                        engine.getTransportManager().getBPM(), 20.0, 999.0, 1, &ok);
                    if (!ok) return;

                    auto& modl = engine.getProjectModel();
                    auto tempoList = modl.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
                    if (!tempoList.isValid())
                    {
                        tempoList = juce::ValueTree(IDs::TEMPO_POINT_LIST);
                        modl.getTree().addChild(tempoList, -1, &modl.getUndoManager());
                    }

                    juce::ValueTree pt(IDs::TEMPO_POINT);
                    pt.setProperty(IDs::startTime, timeSeconds, &modl.getUndoManager());
                    pt.setProperty(IDs::tempo, newBpm, &modl.getUndoManager());
                    tempoList.addChild(pt, -1, &modl.getUndoManager());
                });

                auto* setBpmAction = menu.addAction("Set Global BPM...");
                connect(setBpmAction, &QAction::triggered, this, [this]() {
                    bool ok = false;
                    double newBpm = QInputDialog::getDouble(
                        QApplication::activeWindow(), "Tempo",
                        "BPM:", engine.getTransportManager().getBPM(), 20.0, 999.0, 1, &ok);
                    if (!ok) return;
                    engine.getProjectModel().getTree().setProperty(IDs::tempo, newBpm,
                        &engine.getProjectModel().getUndoManager());
                });

                menu.addSeparator();

                auto* addMidiAction = menu.addAction("Add MIDI Clip...");
                connect(addMidiAction, &QAction::triggered, this, [this, scenePos]() {
                    double timeSeconds = scenePos.x() / pixelsPerSecond;
                    auto& model = engine.getProjectModel();
                    auto trackList = model.getTrackListTree();

                    int trackIndex = timelineScene->trackIndexAtY(scenePos.y());
                    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
                        return;

                    auto trackTree = trackList.getChild(trackIndex);
                    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
                    if (!clipList.isValid())
                    {
                        clipList = juce::ValueTree(IDs::CLIP_LIST);
                        trackTree.addChild(clipList, -1, &model.getUndoManager());
                    }

                    auto clip = ProjectModel::createMidiClipEmpty("MIDI Clip",
                        (std::max)(0.0, timeSeconds), 4.0);
                    clipList.addChild(clip, -1, &model.getUndoManager());
                    engine.getMainProcessor()->rebuildRoutingGraph();
                    timelineScene->rebuildFromValueTree();
                });
            }

            menu.exec(cmEvent->globalPos());
            cmEvent->accept();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TimelineView::handleFileDrop(const QString& filePath, QPointF scenePos)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) return;

    QString ext = fi.suffix().toLower();
    QStringList validExts = {"wav", "aiff", "aif", "mp3", "flac", "ogg"};
    if (!validExts.contains(ext)) return;

    // Map scene position to time and track
    double timeSeconds = scenePos.x() / pixelsPerSecond;
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();

    int trackIndex = timelineScene->trackIndexAtY(scenePos.y());
    if (trackIndex < 0) return;

    // Read actual audio file duration
    double duration = 4.0;
    auto& pool = engine.getProjectPool();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        pool.getFormatManager().createReaderFor(juce::File(filePath.toUtf8().constData())));
    if (reader != nullptr)
    {
        duration = reader->lengthInSamples / reader->sampleRate;
    }

    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &model.getUndoManager());
    }

    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              (std::max)(0.0, timeSeconds), duration,
                                              filePath.toUtf8().constData());
    clipList.addChild(clip, -1, &model.getUndoManager());

    engine.getMainProcessor()->rebuildRoutingGraph();
    timelineScene->rebuildFromValueTree();
}
