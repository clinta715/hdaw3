#include "TimelineScene.h"
#include "../engine/AudioEngine.h"
#include "DebugLog.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>

TimelineScene::TimelineScene(AudioEngine& ae, QObject* parent)
    : QGraphicsScene(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    rebuildFromValueTree();
    setSceneRect(0, 0, 4000, (std::max)(sceneRect().height(), 2000.0));
    HDAW_LOG("TSCtor", QString("sceneRect=(%1,%2) items=%3")
        .arg(sceneRect().width()).arg(sceneRect().height()).arg(items().size()));
}

TimelineScene::~TimelineScene()
{
    detachListener();
    clipItemMap.clear();
}

double TimelineScene::getTrackHeight(int trackIndex) const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::trackHeight, defaultTrackHeight);
    return defaultTrackHeight;
}

ClipItem* TimelineScene::findClipById(int clipId) const
{
    auto it = clipItemMap.find(clipId);
    return (it != clipItemMap.end()) ? it->second : nullptr;
}

void TimelineScene::rebuildFromValueTree()
{
    HDAW_LOG("TSRebuild", QString("START - tracks=%1").arg(engine.getProjectModel().getTrackListTree().getNumChildren()));
    detachListener();

    for (const auto& [id, item] : clipItemMap)
    {
        removeItem(item);
        delete item;
    }
    clipItemMap.clear();

    auto& projectModel = engine.getProjectModel();
    auto trackList = projectModel.getTrackListTree();
    trackCount = trackList.getNumChildren();

    HDAW_LOG("TSRebuild", QString("trackCount=%1").arg(trackCount));

    double currentY = rulerHeight;

    for (int t = 0; t < trackCount; ++t)
    {
        auto trackTree = trackList.getChild(t);
        double trackH = getTrackHeight(t);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);

        if (clipList.isValid())
        {
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                auto clipTree = clipList.getChild(c);
                auto* item = createClipItem(clipTree);
                if (item != nullptr)
                {
                    item->setTrackHeight(trackH);
                    double startX = static_cast<double>(clipTree.getProperty(IDs::startTime)) * pixelsPerSecond;
                    item->setPos(startX, currentY);
                    addItem(item);
                    int clipID = clipTree.getProperty(IDs::clipID);
                    clipItemMap[clipID] = item;
                }
            }
        }

        currentY += trackH;
    }

    setSceneRect(0, 0, 4000, currentY + 20);
    attachListener(projectModel.getTrackListTree());

    HDAW_LOG("TSRebuild", QString("DONE - clipItems=%1 sceneRect=(%2,%3) items=%4")
        .arg(clipItemMap.size())
        .arg(sceneRect().width()).arg(sceneRect().height())
        .arg(items().size()));

    emit trackCountChanged(trackCount);
}

void TimelineScene::setPixelsPerSecond(double pps)
{
    if (pixelsPerSecond == pps) return;
    pixelsPerSecond = pps;

    for (const auto& [id, item] : clipItemMap)
    {
        if (item != nullptr)
        {
            item->setPixelsPerSecond(pps);
            double startX = static_cast<double>(item->getClipTree().getProperty(IDs::startTime)) * pps;
            item->setPos(startX, item->pos().y());
        }
    }

    double totalDuration = 60.0;
    setSceneRect(0, 0, totalDuration * pps + 200, sceneRect().height());
    update();
}

double TimelineScene::getTrackY(int trackIndex) const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    double y = rulerHeight;
    for (int i = 0; i < trackIndex && i < trackList.getNumChildren(); ++i)
        y += getTrackHeight(i);
    return y;
}

int TimelineScene::trackIndexAtY(double y) const
{
    double acc = rulerHeight;
    for (int i = 0; i < getTrackCount(); ++i)
    {
        double h = getTrackHeight(i);
        if (y >= acc && y < acc + h) return i;
        acc += h;
    }
    return -1;
}

void TimelineScene::attachListener(juce::ValueTree tree)
{
    if (listenerRoot.isValid())
        listenerRoot.removeListener(this);
    listenerRoot = tree;
    if (listenerRoot.isValid())
        listenerRoot.addListener(this);
}

void TimelineScene::detachListener()
{
    if (listenerRoot.isValid())
    {
        listenerRoot.removeListener(this);
        listenerRoot = juce::ValueTree();
    }
}

ClipItem* TimelineScene::createClipItem(juce::ValueTree clipTree)
{
    juce::String type = clipTree.getProperty(IDs::clipType).toString();
    double bpm = readModel->getTransport().bpm;
    if (type == "audio")
        return new AudioClipItem(clipTree, pixelsPerSecond, engine.getProjectPool());
    else if (type == "midi")
        return new MidiClipItem(clipTree, pixelsPerSecond, bpm);
    return nullptr;
}

void TimelineScene::removeClipItem(juce::ValueTree clipTree)
{
    int clipID = clipTree.getProperty(IDs::clipID);
    auto it = clipItemMap.find(clipID);
    if (it != clipItemMap.end())
    {
        removeItem(it->second);
        delete it->second;
        clipItemMap.erase(it);
    }
}

void TimelineScene::removeTrackRow(juce::ValueTree removedTrack, int removedIndex)
{
    double removedY = rulerHeight;
    for (int i = 0; i < removedIndex; ++i)
        removedY += getTrackHeight(i);
    double removedH = removedTrack.getProperty(IDs::trackHeight, defaultTrackHeight);

    auto clipList = removedTrack.getChildWithName(IDs::CLIP_LIST);
    if (clipList.isValid())
    {
        for (int i = 0; i < clipList.getNumChildren(); ++i)
        {
            int clipID = clipList.getChild(i).getProperty(IDs::clipID);
            auto it = clipItemMap.find(clipID);
            if (it != clipItemMap.end())
            {
                removeItem(it->second);
                delete it->second;
                clipItemMap.erase(it);
            }
        }
    }

    for (auto& [id, item] : clipItemMap)
    {
        double cy = item->pos().y();
        if (cy >= removedY + removedH)
            item->setPos(item->pos().x(), cy - removedH);
    }

    auto trackList = engine.getProjectModel().getTrackListTree();
    trackCount = trackList.getNumChildren();
    double totalH = rulerHeight;
    for (int i = 0; i < trackCount; ++i)
        totalH += getTrackHeight(i);
    setSceneRect(0, 0, 4000, totalH + 20);
    emit trackCountChanged(trackCount);
}

void TimelineScene::updateClipItem(juce::ValueTree clipTree)
{
    int clipID = clipTree.getProperty(IDs::clipID);
    auto it = clipItemMap.find(clipID);
    if (it != clipItemMap.end() && it->second != nullptr)
    {
        double startX = static_cast<double>(clipTree.getProperty(IDs::startTime)) * pixelsPerSecond;
        it->second->setPos(startX, it->second->pos().y());
        it->second->update();
    }
}

void TimelineScene::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType(IDs::CLIP))
    {
        updateClipItem(tree);
    }
    else if (tree.hasType(IDs::TRACK) && property == IDs::color)
    {
        // Track color changed: repaint every clip on this track so they pick
        // up the new color (ClipItem::getColor resolves it from the track).
        auto clipList = tree.getChildWithName(IDs::CLIP_LIST);
        for (int i = 0; i < clipList.getNumChildren(); ++i)
            updateClipItem(clipList.getChild(i));
    }
}

void TimelineScene::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    if (childWhichHasBeenAdded.hasType(IDs::TRACK))
    {
        rebuildFromValueTree();
        return;
    }

    if (childWhichHasBeenAdded.hasType(IDs::CLIP))
    {
        auto trackTree = parentTree.getParent();
        int trackIndex = -1;
        if (trackTree.isValid() && trackTree.hasType(IDs::TRACK))
        {
            auto trackList = engine.getProjectModel().getTrackListTree();
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == trackTree)
                {
                    trackIndex = i;
                    break;
                }
            }
        }

        auto* item = createClipItem(childWhichHasBeenAdded);
        if (item != nullptr)
        {
            double trackH = (trackIndex >= 0) ? getTrackHeight(trackIndex) : defaultTrackHeight;
            item->setTrackHeight(trackH);
            double startX = static_cast<double>(childWhichHasBeenAdded.getProperty(IDs::startTime)) * pixelsPerSecond;
            double y = getTrackY(trackIndex >= 0 ? trackIndex : 0);
            item->setPos(startX, y);
            addItem(item);
            int clipID = childWhichHasBeenAdded.getProperty(IDs::clipID);
            clipItemMap[clipID] = item;
        }
    }
}

void TimelineScene::valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int index)
{
    juce::ignoreUnused(parentTree);
    if (childWhichHasBeenRemoved.hasType(IDs::CLIP))
        removeClipItem(childWhichHasBeenRemoved);
    else if (childWhichHasBeenRemoved.hasType(IDs::TRACK))
        removeTrackRow(childWhichHasBeenRemoved, index);
}

void TimelineScene::valueTreeChildOrderChanged(juce::ValueTree&, int, int)
{
    rebuildFromValueTree();
}

void TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    HDAW_LOG("TSCPress", QString("button=%1").arg(e->button()));
    if (interaction != nullptr && interaction->handleMousePress(e))
    {
        // Ensure the QGraphicsView viewport has keyboard focus so that
        // Delete/Backspace and other shortcuts work after clicking a clip.
        ensureViewportFocus();
        return;
    }
    // Only delegate non-left-button presses (e.g. right-click for context
    // menus) to the base class if interaction did not handle them. The
    // base class default handler clears selection for right-click on
    // ItemIsSelectable items, which we do not want — right-click must
    // preserve the current multi-selection.
    if (e->button() == Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(e);
        ensureViewportFocus();
    }
}

void TimelineScene::ensureViewportFocus()
{
    if (!views().isEmpty())
    {
        if (auto* vp = views().first()->viewport())
            vp->setFocus();
    }
}

void TimelineScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (interaction != nullptr && interaction->handleMouseMove(e))
        return;
    QGraphicsScene::mouseMoveEvent(e);
}

void TimelineScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (interaction != nullptr && interaction->handleMouseRelease(e))
        return;
    // Only delegate left-button releases to the base class. For
    // non-left buttons (e.g. right-click context menu), the base
    // class default handler can interfere with the current selection.
    if (e->button() == Qt::LeftButton)
        QGraphicsScene::mouseReleaseEvent(e);
}

void TimelineScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e)
{
    if (interaction != nullptr && interaction->handleMouseDoubleClick(e))
        return;
    QGraphicsScene::mouseDoubleClickEvent(e);
}
