#include "TimelineView.h"
#include "Theme.h"
#include <QScrollBar>
#include <QInputDialog>
#include <QApplication>
#include <limits>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QGraphicsView>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QApplication>
#include <QShowEvent>
#include "DebugLog.h"
#include "../engine/AudioImport.h"

TimelineView::TimelineView(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    setupUI();
    connectSignals();

    // Register as a ValueTree listener on the project tree so we can
    // observe MARKER_LIST mutations. The listener is removed in the
    // destructor by the ValueTree's own bookkeeping.
    engine.getProjectModel().getTree().addListener(this);
    syncMarkers();
}

TimelineView::~TimelineView()
{
    engine.getProjectModel().getTree().removeListener(this);
    playheadCursor->stopSync();
    for (auto* m : markerItems)
        if (m) delete m;
    markerItems.clear();
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
    connect(toolbar, static_cast<void (TimelineToolbar::*)(bool)>(&TimelineToolbar::zoomFitClicked),
            this, [this](bool fitAll) {
        if (fitAll) zoomToFitAll();
        else        zoomToFitSelection();
    });

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
    connect(toolbar, &TimelineToolbar::countInToggled, this, &TimelineView::countInToggled);
    connect(toolbar, &TimelineToolbar::timeSigChanged, this, &TimelineView::timeSigChanged);
    connect(toolbar, &TimelineToolbar::midiDeviceChanged, this, &TimelineView::midiDeviceChanged);

    connect(this, &TimelineView::defaultClipLenChanged, this, [this](double beats) {
        interaction->setDefaultClipDuration(beats);
    });
    connect(toolbar, &TimelineToolbar::defaultClipLenChanged, this, &TimelineView::defaultClipLenChanged);

    connect(toolbar, &TimelineToolbar::followPlayheadChanged, this, &TimelineView::setFollowPlayhead);
    connect(toolbar, &TimelineToolbar::recordClicked, this, &TimelineView::recordToggled);
    connect(toolbar, &TimelineToolbar::ccRecordToggled, this, &TimelineView::ccRecordToggled);
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

    // Input monitoring
    connect(trackHeaders, &TrackHeaderWidget::inputMonitoringChanged, this,
        &TimelineView::inputMonitoringChanged);

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

    // Update marker item zoom too
    for (auto* m : markerItems)
        if (m) m->setPixelsPerSecond(pixelsPerSecond);

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

void TimelineView::zoomToFitAll()
{
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();

    double maxEnd = 0.0;
    bool any = false;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double s = clip.getProperty(IDs::startTime);
            double d = clip.getProperty(IDs::duration);
            double e = s + d;
            if (e > maxEnd) maxEnd = e;
            any = true;
        }
    }
    // If no clips exist, fall back to a 30-second default window.
    if (!any)
        maxEnd = 30.0;

    // Pad 5% on either side so the rightmost clip isn't flush against
    // the viewport edge.
    double padded = maxEnd * 1.05;

    int viewportWidth = graphicsView->viewport()->width();
    if (viewportWidth <= 0)
        viewportWidth = 800;

    double targetPps = static_cast<double>(viewportWidth) / padded;
    targetPps = std::clamp(targetPps, minPPS, maxPPS);

    double factor = targetPps / pixelsPerSecond;
    setZoom(factor);

    // Centre the visible region on the content origin.
    graphicsView->horizontalScrollBar()->setValue(0);
}

void TimelineView::zoomToFitSelection()
{
    if (interaction == nullptr) return;

    auto selected = interaction->getSelectedClips();
    if (selected.isEmpty())
    {
        zoomToFitAll();
        return;
    }

    double minStart = std::numeric_limits<double>::max();
    double maxEnd = 0.0;
    for (auto* clip : selected)
    {
        double s = clip->getStartTime();
        double d = clip->getDuration();
        if (s < minStart) minStart = s;
        if (s + d > maxEnd) maxEnd = s + d;
    }

    double span = maxEnd - minStart;
    if (span <= 0.0) span = 1.0;

    // Pad 10% on either side so the bounds aren't flush against the edge.
    double padded = span * 1.10;

    int viewportWidth = graphicsView->viewport()->width();
    if (viewportWidth <= 0)
        viewportWidth = 800;

    double targetPps = static_cast<double>(viewportWidth) / padded;
    targetPps = std::clamp(targetPps, minPPS, maxPPS);

    double factor = targetPps / pixelsPerSecond;
    setZoom(factor);

    // Centre the visible region on the selection.
    int scrollX = static_cast<int>((minStart - span * 0.05) * pixelsPerSecond);
    graphicsView->horizontalScrollBar()->setValue(scrollX);
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

    graphicsView->horizontalScrollBar()->setValue(static_cast<int>(centerX));
}

void TimelineView::cutSelectedClips()
{
    if (interaction == nullptr)
        return;
    auto& um = engine.getProjectModel().getUndoManager();
    um.beginNewTransaction("Cut clips");
    copySelectedClips();
    interaction->deleteSelectedClips();
}

void TimelineView::copySelectedClips()
{
    if (interaction == nullptr)
        return;
    auto selected = interaction->getSelectedClips();
    if (selected.isEmpty())
        return;
    std::vector<HDAW::ClipboardEntry> entries;
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    for (auto* clip : selected)
    {
        auto clipTree = clip->getClipTree();
        auto trackTree = ProjectModel::getTrackOfClip(clipTree);
        int trackIdx = trackList.indexOf(trackTree);
        HDAW::ClipboardEntry entry = HDAW::ClipClipboard::deepCopy(clipTree);
        entry.sourceTrackIndex = trackIdx;
        entries.push_back(entry);
    }
    HDAW::ClipClipboard::copyClips(entries);
}

void TimelineView::pasteClips()
{
    if (!HDAW::ClipClipboard::hasContent())
        return;
    auto& model = engine.getProjectModel();
    auto& um = model.getUndoManager();
    um.beginNewTransaction("Paste clips");

    // Paste origin: current playhead time, in seconds
    int64_t samples = engine.getTransportManager().getCurrentSample();
    double sr = engine.getTransportManager().getSampleRate();
    double originSeconds = sr > 0 ? static_cast<double>(samples) / sr : 0.0;

    auto& entries = HDAW::ClipClipboard::getClips();
    auto trackList = model.getTrackListTree();
    int numTracks = trackList.getNumChildren();

    for (const auto& entry : entries)
    {
        if (!entry.clipTree.isValid()) continue;

        // Choose target track: prefer the source track if it still exists,
        // else fall back to the currently selected track, else track 0.
        int targetTrack = entry.sourceTrackIndex;
        if (targetTrack < 0 || targetTrack >= numTracks)
        {
            if (selectedTrack >= 0 && selectedTrack < numTracks)
                targetTrack = selectedTrack;
            else if (numTracks > 0)
                targetTrack = 0;
            else
                continue;
        }
        auto trackTree = trackList.getChild(targetTrack);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
        {
            clipList = juce::ValueTree(IDs::CLIP_LIST);
            trackTree.addChild(clipList, -1, &um);
        }

        // Offset paste so the earliest source start lands at origin.
        double minStart = HDAW::ClipClipboard::getMeta().minStartTime;
        double srcStart = entry.clipTree.getProperty(IDs::startTime);
        double newStart = originSeconds + (srcStart - minStart);
        if (newStart < 0.0) newStart = 0.0;

        // Deep copy again so each paste is independent.
        auto newClip = entry.clipTree.createCopy();
        newClip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), &um);
        newClip.setProperty(IDs::startTime, newStart, &um);
        // Append " copy" to the name to disambiguate.
        juce::String origName = newClip.getProperty(IDs::name).toString();
        if (!origName.endsWith(" copy"))
            newClip.setProperty(IDs::name, origName + " copy", &um);
        clipList.addChild(newClip, -1, &um);
    }

    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
        mainProc->rebuildRoutingGraph();
}

void TimelineView::duplicateSelectedClips()
{
    if (interaction == nullptr)
        return;
    auto& um = engine.getProjectModel().getUndoManager();
    um.beginNewTransaction("Duplicate clips");

    // Build local copies without touching the global clipboard.
    auto selected = interaction->getSelectedClips();
    if (selected.isEmpty())
        return;
    std::vector<HDAW::ClipboardEntry> entries;
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    for (auto* clip : selected)
    {
        auto clipTree = clip->getClipTree();
        auto trackTree = ProjectModel::getTrackOfClip(clipTree);
        int trackIdx = trackList.indexOf(trackTree);
        HDAW::ClipboardEntry entry = HDAW::ClipClipboard::deepCopy(clipTree);
        entry.sourceTrackIndex = trackIdx;
        entries.push_back(entry);
    }

    int numTracks = trackList.getNumChildren();

    for (const auto& entry : entries)
    {
        if (!entry.clipTree.isValid()) continue;
        int targetTrack = entry.sourceTrackIndex;
        if (targetTrack < 0 || targetTrack >= numTracks) continue;
        auto trackTree = trackList.getChild(targetTrack);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;

        auto newClip = entry.clipTree.createCopy();
        newClip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), &um);
        double srcStart2 = entry.clipTree.getProperty(IDs::startTime);
        double newStart = srcStart2 + duplicateOffsetSeconds;
        newClip.setProperty(IDs::startTime, newStart, &um);
        juce::String origName = newClip.getProperty(IDs::name).toString();
        if (!origName.endsWith(" copy"))
            newClip.setProperty(IDs::name, origName + " copy", &um);
        clipList.addChild(newClip, -1, &um);
    }

    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
        mainProc->rebuildRoutingGraph();
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
            handleDrop(static_cast<QDropEvent*>(event));
            return true;
        }
        else if (event->type() == QEvent::KeyPress)
        {
            handleKeyPress(static_cast<QKeyEvent*>(event));
            return true;
        }
        else if (event->type() == QEvent::Wheel)
        {
            auto* we = static_cast<QWheelEvent*>(event);
            if (we->modifiers() & Qt::ShiftModifier)
            {
                graphicsView->horizontalScrollBar()->setValue(
                    graphicsView->horizontalScrollBar()->value() - we->angleDelta().y());
                return true;
            }
        }
        else if (event->type() == QEvent::ContextMenu)
        {
            handleContextMenu(static_cast<QContextMenuEvent*>(event));
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TimelineView::handleContextMenu(QContextMenuEvent* event)
{
    QPointF scenePos = graphicsView->mapToScene(event->pos());
    QGraphicsItem* item = timelineScene->itemAt(scenePos, QTransform());

    // No parentItem() walk: ClipItem is not grouped under track-lane containers.
    ClipItem* clip = dynamic_cast<ClipItem*>(item);

    if (clip != nullptr)
        handleClipContextMenu(clip, event->globalPos());
    else
        handleEmptyAreaContextMenu(scenePos, event->globalPos());

    event->accept();
}

void TimelineView::handleClipContextMenu(ClipItem* clip, const QPoint& globalPos)
{
    QMenu menu;

    auto clipTree = clip->getClipTree();

    if (clipTree.getProperty(IDs::clipType).toString() == "audio")
    {
        auto takeList = clipTree.getChildWithName(IDs::TAKE_LIST);
        if (takeList.isValid() && takeList.getNumChildren() > 1)
        {
            auto* takesMenu = menu.addMenu("Takes");
            int activeIdx = clipTree.getProperty(IDs::activeTake, 0);
            for (int t = 0; t < takeList.getNumChildren(); ++t)
            {
                auto take = takeList.getChild(t);
                QString name = QString::fromUtf8(take.getProperty(IDs::name).toString().toRawUTF8());
                auto* action = takesMenu->addAction(name);
                action->setCheckable(true);
                action->setChecked(t == activeIdx);
                auto trackTree = clipTree.getParent().getParent();
                auto& um = engine.getProjectModel().getUndoManager();
                connect(action, &QAction::triggered, this, [this, clipTree, takeList, t, trackTree, &um]() mutable {
                    clipTree.setProperty(IDs::activeTake, t, &um);
                    auto sourceFile = takeList.getChild(t).getProperty(IDs::sourceFile).toString();
                    auto trackList = engine.getProjectModel().getTrackListTree();
                    int trackIdx = trackList.indexOf(trackTree);
                    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
                    int clipIdx = clipList.indexOf(clipTree);
                    if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
                    {
                        if (auto* rm = mainProc->getRoutingManager())
                            rm->switchClipTake(trackIdx, clipIdx, sourceFile);
                    }
                });
            }
            menu.addSeparator();
        }

        // Audio editing actions
        auto* normalizeAction = menu.addAction("Normalize");
        connect(normalizeAction, &QAction::triggered, this, [this, clip]() {
            QString sourcePath = QString::fromUtf8(clip->getClipTree().getProperty(IDs::sourceFile).toString().toRawUTF8());
            QString outPath;
            if (HDAW::normalizeAudioFile(engine, sourcePath, outPath))
            {
                auto& um = engine.getProjectModel().getUndoManager();
                clip->getClipTree().setProperty(IDs::sourceFile, outPath.toUtf8().constData(), &um);
                if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
                    mainProc->rebuildRoutingGraph();
            }
        });

        auto* reverseAction = menu.addAction("Reverse");
        connect(reverseAction, &QAction::triggered, this, [this, clip]() {
            QString sourcePath = QString::fromUtf8(clip->getClipTree().getProperty(IDs::sourceFile).toString().toRawUTF8());
            QString outPath;
            if (HDAW::reverseAudioFile(engine, sourcePath, outPath))
            {
                auto& um = engine.getProjectModel().getUndoManager();
                clip->getClipTree().setProperty(IDs::sourceFile, outPath.toUtf8().constData(), &um);
                if (auto* mainProc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
                    mainProc->rebuildRoutingGraph();
            }
        });
        menu.addSeparator();
    }

    int selectedCount = 0;
    for (auto* item : timelineScene->selectedItems())
        if (dynamic_cast<ClipItem*>(item) != nullptr) ++selectedCount;

    auto* copyAction = menu.addAction("Copy");
    connect(copyAction, &QAction::triggered, this, [this]() { copySelectedClips(); });

    auto* cutAction = menu.addAction("Cut");
    connect(cutAction, &QAction::triggered, this, [this]() { cutSelectedClips(); });

    auto* duplicateAction = menu.addAction("Duplicate");
    connect(duplicateAction, &QAction::triggered, this, [this]() { duplicateSelectedClips(); });

    auto* pasteAction = menu.addAction("Paste");
    connect(pasteAction, &QAction::triggered, this, [this]() { pasteClips(); });
    pasteAction->setEnabled(HDAW::ClipClipboard::hasContent());

    auto* deleteAction = menu.addAction(selectedCount > 1
        ? QString("Delete %1 Clips").arg(selectedCount)
        : QStringLiteral("Delete Clip"));
    connect(deleteAction, &QAction::triggered, this, [this, clip]() {
        if (interaction == nullptr)
        {
            auto ct = clip->getClipTree();
            auto parentTree = ct.getParent();
            if (parentTree.isValid())
                parentTree.removeChild(ct, &engine.getProjectModel().getUndoManager());
            return;
        }
        auto selected = interaction->getSelectedClips();
        if (selected.size() <= 1)
        {
            // Single clip — use direct removal to preserve the focused clip
            auto ct = clip->getClipTree();
            auto parentTree = ct.getParent();
            if (parentTree.isValid())
                parentTree.removeChild(ct, &engine.getProjectModel().getUndoManager());
        }
        else
        {
            interaction->deleteSelectedClips();
        }
    });

    auto* openAction = menu.addAction("Open in Editor");
    connect(openAction, &QAction::triggered, this, [this, clip]() {
        emit timelineScene->clipSelected(clip->getClipTree());
    });

    menu.addSeparator();

    auto* splitAction = menu.addAction("Split");
    connect(splitAction, &QAction::triggered, this, []() {
    });

    menu.exec(globalPos);
}

void TimelineView::handleEmptyAreaContextMenu(const QPointF& scenePos, const QPoint& globalPos)
{
    QMenu menu;

    double timeSeconds = scenePos.x() / pixelsPerSecond;

    auto* addTrackAction = menu.addAction("Add Track");
    connect(addTrackAction, &QAction::triggered, this, &TimelineView::addTrackClicked);

    menu.addSeparator();

    auto* pasteAtCursor = menu.addAction("Paste");
    connect(pasteAtCursor, &QAction::triggered, this, [this]() { pasteClips(); });
    pasteAtCursor->setEnabled(HDAW::ClipClipboard::hasContent());

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
    });

    menu.exec(globalPos);
}

void TimelineView::handleKeyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (interaction != nullptr)
            interaction->deleteSelectedClips();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier))
    {
        // Ctrl+A — select all clips
        if (interaction != nullptr)
        {
            for (auto* item : timelineScene->items())
            {
                auto* ci = dynamic_cast<ClipItem*>(item);
                if (ci != nullptr)
                    ci->setSelected(true);
            }
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape)
    {
        if (interaction != nullptr)
        {
            for (auto* item : timelineScene->items())
                item->setSelected(false);
        }
        event->accept();
        return;
    }

    Qt::KeyboardModifiers mods = event->modifiers();
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        copySelectedClips();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_X)
    {
        cutSelectedClips();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_V)
    {
        pasteClips();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_D)
    {
        duplicateSelectedClips();
        event->accept();
        return;
    }
}

void TimelineView::handleDrop(QDropEvent* event)
{
    QString filePath;

    if (event->mimeData()->hasUrls())
    {
        auto urls = event->mimeData()->urls();
        if (!urls.isEmpty())
            filePath = urls.first().toLocalFile();
    }
    else if (event->mimeData()->hasText())
    {
        filePath = event->mimeData()->text();
    }

    if (!filePath.isEmpty())
    {
        QPointF scenePos = graphicsView->mapToScene(event->position().toPoint());
        handleFileDrop(filePath, scenePos);
        event->acceptProposedAction();
    }
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
}

// ------------------------------------------------------------------
// Marker management
// ------------------------------------------------------------------

void TimelineView::syncMarkers()
{
    auto& model = engine.getProjectModel();
    auto projectTree = model.getTree();
    auto newList = projectTree.getChildWithName(IDs::MARKER_LIST);

    // Drop removed items, add new items. Properties (name, time, color)
    // are reflected in the existing MarkerItem via its paint() so we
    // just need to call update().
    if (markerListTree != newList)
    {
        // List pointer changed (e.g. after a project load) — rebuild from
        // scratch.
        for (auto* m : markerItems)
            if (m) delete m;
        markerItems.clear();
        markerListTree = newList;
    }
    if (!markerListTree.isValid())
        return;

    // Remove MarkerItem entries whose ValueTree is no longer in the list.
    for (size_t i = 0; i < markerItems.size(); )
    {
        auto* m = markerItems[i];
        bool found = false;
        for (int j = 0; j < markerListTree.getNumChildren(); ++j)
            if (markerListTree.getChild(j) == m->getMarkerTree()) { found = true; break; }
        if (!found)
        {
            delete m;
            markerItems.erase(markerItems.begin() + i);
        }
        else
        {
            ++i;
        }
    }

    // Add new items for any marker we don't have a MarkerItem for.
    for (int j = 0; j < markerListTree.getNumChildren(); ++j)
    {
        auto markerTree = markerListTree.getChild(j);
        bool found = false;
        for (auto* m : markerItems)
            if (m && m->getMarkerTree() == markerTree) { found = true; break; }
        if (!found)
            onMarkerAdded(markerTree);
    }

    for (auto* m : markerItems)
        if (m) m->setPixelsPerSecond(pixelsPerSecond);
}

MarkerItem* TimelineView::findMarkerItem(const juce::ValueTree& markerTree)
{
    for (auto* m : markerItems)
        if (m && m->getMarkerTree() == markerTree)
            return m;
    return nullptr;
}

void TimelineView::onMarkerAdded(const juce::ValueTree& markerTree)
{
    auto* m = new MarkerItem(markerTree, engine);
    m->setPixelsPerSecond(pixelsPerSecond);
    timelineScene->addItem(m);

    // Place at the marker's stored time, just above the time ruler.
    double t = markerTree.getProperty(IDs::markerTime);
    m->setPos(t * pixelsPerSecond, rulerItem->pos().y());

    connect(m, &MarkerItem::markerClicked, this, [this](double time) {
        // Seek the playhead to the marker position.
        auto& tm = engine.getTransportManager();
        double sr = tm.getSampleRate();
        if (sr <= 0) sr = 44100.0;
        tm.setCurrentSample(static_cast<int64_t>(time * sr));
        auto transport = engine.getProjectModel().getTransportTree();
        transport.setProperty(IDs::position, time, nullptr);
    });
    connect(m, &MarkerItem::markerDeleteRequested, this, [this](juce::ValueTree tree) {
        if (markerListTree.isValid() && tree.getParent() == markerListTree)
            markerListTree.removeChild(tree, &engine.getProjectModel().getUndoManager());
    });
    connect(m, &MarkerItem::markerRenameRequested, this, [this](juce::ValueTree tree) {
        bool ok = false;
        QString newName = QInputDialog::getText(
            QApplication::activeWindow(), "Rename Marker", "Marker name:",
            QLineEdit::Normal, QString::fromUtf8(tree.getProperty(IDs::markerName).toString().toRawUTF8()),
            &ok);
        if (ok)
            tree.setProperty(IDs::markerName, newName.toUtf8().constData(),
                             &engine.getProjectModel().getUndoManager());
    });

    markerItems.push_back(m);
}

void TimelineView::onMarkerRemoved(const juce::ValueTree& markerTree)
{
    for (size_t i = 0; i < markerItems.size(); ++i)
    {
        if (markerItems[i] && markerItems[i]->getMarkerTree() == markerTree)
        {
            delete markerItems[i];
            markerItems.erase(markerItems.begin() + i);
            return;
        }
    }
}

void TimelineView::onMarkerPropertyChanged(const juce::ValueTree& markerTree,
                                           const juce::Identifier& property)
{
    auto* m = findMarkerItem(markerTree);
    if (m == nullptr) return;
    if (property == IDs::markerTime)
    {
        double t = markerTree.getProperty(IDs::markerTime);
        m->setPos(t * pixelsPerSecond, rulerItem->pos().y());
    }
    m->update();
}

void TimelineView::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                            const juce::Identifier& property)
{
    if (treeWhosePropertyHasChanged.hasType(IDs::MARKER))
        onMarkerPropertyChanged(treeWhosePropertyHasChanged, property);
}

void TimelineView::valueTreeChildAdded(juce::ValueTree& parentTree,
                                       juce::ValueTree& childWhichHasBeenAdded)
{
    if (parentTree.hasType(IDs::PROJECT) && childWhichHasBeenAdded.hasType(IDs::MARKER_LIST))
    {
        markerListTree = childWhichHasBeenAdded;
        syncMarkers();
    }
    else if (parentTree == markerListTree && childWhichHasBeenAdded.hasType(IDs::MARKER))
    {
        onMarkerAdded(childWhichHasBeenAdded);
    }
}

void TimelineView::valueTreeChildRemoved(juce::ValueTree& parentTree,
                                         juce::ValueTree& childWhichHasBeenRemoved,
                                         int indexFromWhichItWasRemoved)
{
    juce::ignoreUnused(indexFromWhichItWasRemoved);
    if (parentTree == markerListTree && childWhichHasBeenRemoved.hasType(IDs::MARKER))
    {
        onMarkerRemoved(childWhichHasBeenRemoved);
    }
}
