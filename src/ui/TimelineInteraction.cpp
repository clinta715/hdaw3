#include "TimelineInteraction.h"
#include "TimelineScene.h"
#include "ClipItem.h"
#include "../engine/AudioEngine.h"
#include "DebugLog.h"
#include "Theme.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QBrush>
#include <QPen>
#include <cmath>

TimelineInteraction::TimelineInteraction(TimelineScene* s, AudioEngine& ae, QObject* parent)
    : QObject(parent), engine(ae), scene(s)
{
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
}

TimelineInteraction::~TimelineInteraction() = default;

double TimelineInteraction::snapToGrid(double timeSeconds) const
{
    if (!snapEnabled || snapDivision == Off)
        return timeSeconds;

    double bpm = (std::max)(1.0, readModel->getTransport().bpm);
    double secondsPerBeat = 60.0 / bpm;

    double division;
    switch (snapDivision)
    {
        case Bar:      division = secondsPerBeat * 4.0; break;
        case Beat:     division = secondsPerBeat; break;
        case Eighth:   division = secondsPerBeat / 2.0; break;
        case Sixteenth: division = secondsPerBeat / 4.0; break;
        default:       return timeSeconds;
    }

    return std::round(timeSeconds / division) * division;
}

bool TimelineInteraction::handleMousePress(QGraphicsSceneMouseEvent* e)
{
    HDAW_LOG("TIPress", QString("button=%1 pos=(%2,%3) sceneChildren=%4")
        .arg(e->button())
        .arg(e->scenePos().x()).arg(e->scenePos().y())
        .arg(scene->items().size()));

    if (e->button() != Qt::LeftButton)
        return false;

    Qt::KeyboardModifiers mods = e->modifiers();
    bool additive = mods & Qt::ControlModifier;
    bool range = mods & Qt::ShiftModifier;

    QGraphicsItem* item = scene->itemAt(e->scenePos(), QTransform());
    HDAW_LOG("TIPress", QString("itemAt type=%1 mods=%2").arg(item ? item->type() : -1).arg(int(mods)));

    // Check for edge/fade handles on clip items.
    // No parentItem() walk: ClipItem is not grouped under track-lane containers.
    ClipItem* clip = dynamic_cast<ClipItem*>(item);

    if (clip != nullptr)
    {
        // Multi-selection: handle selection before drag.
        // If user clicked in a non-trim/non-fade area, this also changes
        // selection. Edge/trim/fade drags preserve the existing selection.
        double pps = clip->getPixelsPerSecond();
        double dur = clip->getDuration();
        double clipW = dur * pps;
        QPointF localPos = clip->mapFromScene(e->scenePos());
        double edgeThreshold = 6.0;

        // If clicking near an edge or fade handle, treat as a trim/fade
        // drag on this clip only and DO NOT change selection.
        bool isEdgeArea = (localPos.x() < edgeThreshold)
                       || (localPos.x() > clipW - edgeThreshold);
        bool isFadeArea = false;
        if (localPos.y() < 10)
        {
            double fadeIn = clip->getFadeIn();
            double fadeOut = clip->getFadeOut();
            if ((localPos.x() < fadeIn * pps + edgeThreshold && fadeIn > 0.001)
             || (localPos.x() > clipW - fadeOut * pps - edgeThreshold && fadeOut > 0.001))
                isFadeArea = true;
        }

        if (!isEdgeArea && !isFadeArea)
        {
            // Plain click on clip body — update selection.
            if (additive)
            {
                clip->setSelected(!clip->isSelected());
                lastClickedClip = clip;
            }
            else if (range && lastClickedClip != nullptr)
            {
                selectRange(lastClickedClip, clip);
                lastClickedClip = clip;
            }
            else
            {
                // Clear others, select this one
                clearSelection();
                clip->setSelected(true);
                lastClickedClip = clip;
            }
        }

        if (undoManager)
            undoManager->beginNewTransaction("Edit clip");

        dragPPS = pps;

        // Trim left edge
        if (isEdgeArea && localPos.x() < edgeThreshold)
        {
            dragMode = TrimLeft;
            dragItem = clip;
            dragStartValue = clip->getStartTime();
            e->accept();
            return true;
        }
        if (isEdgeArea && localPos.x() > clipW - edgeThreshold)
        {
            dragMode = TrimRight;
            dragItem = clip;
            dragStartValue = clip->getDuration();
            e->accept();
            return true;
        }
        if (isFadeArea)
        {
            double fadeIn = clip->getFadeIn();
            double fadeOut = clip->getFadeOut();
            if (localPos.x() < fadeIn * pps + edgeThreshold && fadeIn > 0.001)
            {
                dragMode = FadeIn;
                dragItem = clip;
                dragStartValue = fadeIn;
                e->accept();
                return true;
            }
            if (localPos.x() > clipW - fadeOut * pps - edgeThreshold && fadeOut > 0.001)
            {
                dragMode = FadeOut;
                dragItem = clip;
                dragStartValue = fadeOut;
                e->accept();
                return true;
            }
        }

        // Default: move
        dragMode = Move;
        dragItem = clip;
        dragStartPos = e->scenePos();
        dragStartValue = clip->getStartTime();
        e->accept();
        return true;
    }

    // Not a ClipItem — start rubber band selection on empty area.
    // LoopMarker/TimeRuler/etc. handle their own events; we only run
    // when no item was hit, so this is genuinely the empty track area.
    if (item == nullptr)
    {
        startRubberBand(e->scenePos(), additive || range);
        e->accept();
        return true;
    }
    // Some other item (ruler, marker, etc.) — let the scene dispatch.
    return false;
}

bool TimelineInteraction::handleMouseMove(QGraphicsSceneMouseEvent* e)
{
    if (dragMode == None)
        return false;

    if (dragMode == RubberBand)
    {
        updateRubberBand(e->scenePos());
        e->accept();
        return true;
    }

    double pps = dragPPS;

    if (dragMode == Move && dragItem != nullptr)
    {
        QPointF delta = e->scenePos() - dragStartPos;
        double newTime = dragStartValue + delta.x() / pps;
        newTime = snapToGrid(newTime);
        newTime = (std::max)(0.0, newTime);

        // Move all selected clips by the same delta so multi-selection
        // drags stay aligned.
        double timeDelta = newTime - dragItem->getStartTime();
        auto* um = undoManager;
        if (um && !getSelectedClips().contains(dragItem))
        {
            // Drag was started on an unselected clip (we still let the
            // move happen on dragItem only).
        }
        auto selected = getSelectedClips();
        for (auto* c : selected)
        {
            double targetStart = c->getStartTime() + timeDelta;
            targetStart = (std::max)(0.0, targetStart);
            c->getClipTree().setProperty(IDs::startTime, targetStart, um);
            c->setPos(targetStart * pps, c->pos().y());
        }
        // If the dragged clip wasn't in the selection (e.g. rubber band
        // selected nothing), still move it alone.
        if (!selected.contains(dragItem))
        {
            dragItem->getClipTree().setProperty(IDs::startTime, newTime, undoManager);
            dragItem->setPos(newTime * pps, dragItem->pos().y());
        }
        e->accept();
        return true;
    }

    if ((dragMode == TrimLeft || dragMode == TrimRight) && dragItem != nullptr)
    {
        QPointF localPos = dragItem->mapFromScene(e->scenePos());
        double timeOffset = localPos.x() / pps;

        if (dragMode == TrimLeft)
        {
            double oldStart = dragStartValue;
            double oldDur = dragItem->getDuration();
            double newStart = dragStartValue + timeOffset;
            newStart = snapToGrid(newStart);
            newStart = (std::max)(0.0, (std::min)(newStart, oldStart + oldDur - 0.1));
            double newDur = oldDur - (newStart - oldStart);
            newDur = (std::min)(newDur, 3600.0);
            dragItem->getClipTree().setProperty(IDs::startTime, newStart, undoManager);
            dragItem->getClipTree().setProperty(IDs::duration, newDur, undoManager);
        }
        else // TrimRight
        {
            double newDur = (std::max)(0.1, dragStartValue + timeOffset);
            newDur = snapToGrid(dragItem->getStartTime() + newDur) - dragItem->getStartTime();
            newDur = (std::max)(0.1, newDur);
            dragItem->getClipTree().setProperty(IDs::duration, newDur, undoManager);
            lastClipDuration = newDur;
        }

        dragItem->setPos(dragItem->getStartTime() * pps, dragItem->pos().y());
        dragItem->update();
        e->accept();
        return true;
    }

    if ((dragMode == FadeIn || dragMode == FadeOut) && dragItem != nullptr)
    {
        QPointF localPos = dragItem->mapFromScene(e->scenePos());
        double newFade;
        double maxFade = dragItem->getDuration() * 0.5;

        if (dragMode == FadeIn)
        {
            newFade = (std::max)(0.0, localPos.x() / pps);
            newFade = (std::min)(newFade, maxFade);
            dragItem->getClipTree().setProperty(IDs::fadeIn, newFade, undoManager);
        }
        else
        {
            newFade = (std::max)(0.0, dragItem->getDuration() - localPos.x() / pps);
            newFade = (std::min)(newFade, maxFade);
            dragItem->getClipTree().setProperty(IDs::fadeOut, newFade, undoManager);
        }
        dragItem->update();
        e->accept();
        return true;
    }

    return false;
}

bool TimelineInteraction::handleMouseRelease(QGraphicsSceneMouseEvent* e)
{
    if (dragMode == RubberBand)
    {
        endRubberBand();
        e->accept();
        return true;
    }
    if (dragMode != None)
    {
        dragMode = None;
        dragItem = nullptr;
        return true;
    }
    return false;
}

bool TimelineInteraction::handleMouseDoubleClick(QGraphicsSceneMouseEvent* e)
{
    HDAW_LOG("TIDblClk", QString("pos=(%1,%2) sceneChildren=%3")
        .arg(e->scenePos().x()).arg(e->scenePos().y())
        .arg(scene->items().size()));

    QGraphicsItem* item = scene->itemAt(e->scenePos(), QTransform());
    HDAW_LOG("TIDblClk", QString("itemAt type=%1").arg(item ? item->type() : -1));
    // No parentItem() walk: ClipItem is not grouped under track-lane containers.
    ClipItem* clip = dynamic_cast<ClipItem*>(item);

    if (clip != nullptr)
    {
        emit scene->clipSelected(clip->getClipTree());
        e->accept();
        return true;
    }

    // Double-click on empty track area → create empty MIDI clip
    double pps = scene->getPixelsPerSecond();
    double clickTime = e->scenePos().x() / pps;
    double snappedTime = snapToGrid((std::max)(0.0, clickTime));
    double clickY = e->scenePos().y();

    // Find which track the Y position falls on
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    int trackIndex = scene->trackIndexAtY(clickY);
    HDAW_LOG("TIDblClk", QString("clickY=%1 numTracks=%2 trackIndex=%3")
        .arg(clickY).arg(trackList.getNumChildren()).arg(trackIndex));
    if (trackIndex < 0) return false;

    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &model.getUndoManager());
    }

    double duration = (std::max)(lastClipDuration, defaultClipDuration);
    duration = snapToGrid(snappedTime + duration) - snappedTime;
    duration = (std::max)(0.5, duration);

    auto clipTree = ProjectModel::createMidiClipEmpty("MIDI Clip", snappedTime, duration);

    clipList.addChild(clipTree, -1, &model.getUndoManager());
    audioGraphCmds->rebuildRoutingGraph();

    lastClipDuration = duration;
    emit scene->clipSelected(clipTree);
    e->accept();
    return true;
}

// ------------------------------------------------------------------
// Multi-selection helpers
// ------------------------------------------------------------------

void TimelineInteraction::clearSelection()
{
    for (auto* item : scene->selectedItems())
        item->setSelected(false);
    lastClickedClip = nullptr;
}

void TimelineInteraction::selectClip(ClipItem* clip, bool additive)
{
    if (clip == nullptr)
        return;
    if (!additive)
        clearSelection();
    clip->setSelected(true);
    lastClickedClip = clip;
}

void TimelineInteraction::selectRange(ClipItem* fromClip, ClipItem* toClip)
{
    if (fromClip == nullptr || toClip == nullptr)
        return;
    auto fromTrack = ProjectModel::getTrackOfClip(fromClip->getClipTree());
    auto toTrack = ProjectModel::getTrackOfClip(toClip->getClipTree());
    if (!fromTrack.isValid() || fromTrack != toTrack)
    {
        // Different tracks — just select the destination.
        clearSelection();
        toClip->setSelected(true);
        return;
    }
    // Same track: select every clip on that track between the two.
    double fromStart = fromClip->getStartTime();
    double toStart = toClip->getStartTime();
    double lo = (std::min)(fromStart, toStart);
    double hi = (std::max)(fromStart, toStart);
    auto clipList = fromTrack.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
        return;
    clearSelection();
    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        auto child = clipList.getChild(i);
        double s = child.getProperty(IDs::startTime);
        if (s >= lo - 0.0001 && s <= hi + 0.0001)
        {
            // Find the corresponding ClipItem by matching ValueTree.
            for (auto* item : scene->items())
            {
                auto* ci = dynamic_cast<ClipItem*>(item);
                if (ci != nullptr && ci->getClipTree() == child)
                {
                    ci->setSelected(true);
                    break;
                }
            }
        }
    }
}

QList<ClipItem*> TimelineInteraction::getSelectedClips() const
{
    QList<ClipItem*> out;
    for (auto* item : scene->selectedItems())
    {
        auto* ci = dynamic_cast<ClipItem*>(item);
        if (ci != nullptr)
            out << ci;
    }
    return out;
}

void TimelineInteraction::deleteSelectedClips()
{
    auto& um = engine.getProjectModel().getUndoManager();
    if (undoManager)
        undoManager->beginNewTransaction("Delete clips");
    auto selected = getSelectedClips();
    for (auto* clip : selected)
    {
        auto clipTree = clip->getClipTree();
        auto parentTree = clipTree.getParent();
        if (parentTree.isValid())
            parentTree.removeChild(clipTree, &um);
    }
}

void TimelineInteraction::startRubberBand(const QPointF& scenePos, bool additive)
{
    dragMode = RubberBand;
    rubberBandAdditive = additive;
    rubberBandStartPos = scenePos;

    if (!additive)
        clearSelection();

    if (rubberBandRectItem == nullptr)
    {
        rubberBandRectItem = new QGraphicsRectItem();
        rubberBandRectItem->setZValue(1000);
        QPen pen(ThemeColors::accent(), 1, Qt::DashLine);
        QColor brushColor = ThemeColors::accent();
        brushColor.setAlpha(40);
        QBrush brush(brushColor);
        rubberBandRectItem->setPen(pen);
        rubberBandRectItem->setBrush(brush);
        scene->addItem(rubberBandRectItem);
    }
    rubberBandRectItem->setRect(QRectF(scenePos, QSizeF(0, 0)));
    rubberBandRectItem->setVisible(true);
}

void TimelineInteraction::updateRubberBand(const QPointF& scenePos)
{
    QRectF r(rubberBandStartPos, scenePos);
    r = r.normalized();
    rubberBandRectItem->setRect(r);

    // Live preview: highlight all clips that intersect the rect.
    // We don't commit selection until release, but the user wants feedback.
    for (auto* item : scene->items())
    {
        auto* ci = dynamic_cast<ClipItem*>(item);
        if (ci == nullptr) continue;
        bool inside = r.intersects(ci->sceneBoundingRect());
        bool keep = inside;
        if (rubberBandAdditive)
            keep = inside || ci->isSelected();
        else
            keep = inside;
        ci->setSelected(keep);
    }
}

void TimelineInteraction::endRubberBand()
{
    if (rubberBandRectItem != nullptr)
        rubberBandRectItem->setVisible(false);
    dragMode = None;
    // Final selection is whatever updateRubberBand last set.
}
