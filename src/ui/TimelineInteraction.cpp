#include "TimelineInteraction.h"
#include "TimelineScene.h"
#include "ClipItem.h"
#include "DebugLog.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <cmath>

TimelineInteraction::TimelineInteraction(TimelineScene* s, AudioEngine& ae, QObject* parent)
    : QObject(parent), engine(ae), scene(s)
{
}

TimelineInteraction::~TimelineInteraction() = default;

double TimelineInteraction::snapToGrid(double timeSeconds) const
{
    if (!snapEnabled || snapDivision == Off)
        return timeSeconds;

    double bpm = (std::max)(1.0, scene->getEngine().getTransportManager().getBPM());
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

    QGraphicsItem* item = scene->itemAt(e->scenePos(), QTransform());
    HDAW_LOG("TIPress", QString("itemAt type=%1").arg(item ? item->type() : -1));

    // Check for edge/fade handles on clip items.
    // No parentItem() walk: ClipItem is not grouped under track-lane containers.
    ClipItem* clip = dynamic_cast<ClipItem*>(item);

    if (clip != nullptr)
    {
        if (undoManager)
            undoManager->beginNewTransaction("Edit clip");

        double pps = clip->getPixelsPerSecond();
        double dur = clip->getDuration();
        double clipW = dur * pps;
        QPointF localPos = clip->mapFromScene(e->scenePos());
        double edgeThreshold = 6.0;
        dragPPS = pps;

        // Trim left edge
        if (localPos.x() < edgeThreshold)
        {
            dragMode = TrimLeft;
            dragItem = clip;
            dragStartValue = clip->getStartTime();
            e->accept();
            return true;
        }
        // Trim right edge
        if (localPos.x() > clipW - edgeThreshold)
        {
            dragMode = TrimRight;
            dragItem = clip;
            dragStartValue = clip->getDuration();
            e->accept();
            return true;
        }
        // Fade handles (top corners)
        if (localPos.y() < 10)
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
        clip->setSelected(true);
        e->accept();
        return true;
    }

    // Not a ClipItem — let QGraphicsScene dispatch to the actual item
    // underneath (LoopMarker, TimeRuler, etc.)
    return false;
}

bool TimelineInteraction::handleMouseMove(QGraphicsSceneMouseEvent* e)
{
    if (dragMode == None)
        return false;

    double pps = dragPPS;

    if (dragMode == Move && dragItem != nullptr)
    {
        QPointF delta = e->scenePos() - dragStartPos;
        double newTime = dragStartValue + delta.x() / pps;
        newTime = snapToGrid(newTime);
        newTime = (std::max)(0.0, newTime);

        dragItem->getClipTree().setProperty(IDs::startTime, newTime, undoManager);
        dragItem->setPos(newTime * pps, dragItem->pos().y());
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
    double trackY = scene->getRulerHeight();
    int trackIndex = -1;
    HDAW_LOG("TIDblClk", QString("clickY=%1 numTracks=%2").arg(clickY).arg(trackList.getNumChildren()));
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        double h = scene->getTrackHeight(t);
        HDAW_LOG("TIDblClk", QString("  track %1: y=%2 h=%3 clickY in [y,y+h)? %4")
            .arg(t).arg(trackY).arg(h)
            .arg(clickY >= trackY && clickY < trackY + h));
        if (clickY >= trackY && clickY < trackY + h)
        {
            trackIndex = t;
            break;
        }
        trackY += h;
    }
    HDAW_LOG("TIDblClk", QString("trackIndex=%1 clickY=%2 trackY start=%3")
        .arg(trackIndex).arg(clickY).arg(scene->getRulerHeight()));
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

    juce::ValueTree clipTree(IDs::CLIP);
    clipTree.setProperty(IDs::clipID, engine.getProjectModel().allocateClipID(), nullptr);
    clipTree.setProperty(IDs::name, "MIDI Clip", &model.getUndoManager());
    clipTree.setProperty(IDs::startTime, snappedTime, &model.getUndoManager());
    clipTree.setProperty(IDs::duration, duration, &model.getUndoManager());
    clipTree.setProperty(IDs::offset, 0.0, &model.getUndoManager());
    clipTree.setProperty(IDs::clipType, "midi", &model.getUndoManager());
    clipTree.setProperty(IDs::gain, 1.0, &model.getUndoManager());
    clipTree.setProperty(IDs::fadeIn, 0.0, &model.getUndoManager());
    clipTree.setProperty(IDs::fadeOut, 0.0, &model.getUndoManager());
    clipTree.setProperty(IDs::looping, false, &model.getUndoManager());
    clipTree.setProperty(IDs::color, static_cast<int>(0xFFCC8844), &model.getUndoManager());

    juce::ValueTree midiNotes(IDs::MIDI_NOTE_LIST);
    clipTree.addChild(midiNotes, -1, nullptr);

    clipList.addChild(clipTree, -1, &model.getUndoManager());
    engine.getMainProcessor()->rebuildRoutingGraph();
    scene->rebuildFromValueTree();

    lastClipDuration = duration;
    emit scene->clipSelected(clipTree);
    e->accept();
    return true;
}
