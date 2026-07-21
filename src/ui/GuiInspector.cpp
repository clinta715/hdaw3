#include "GuiInspector.h"

#ifdef HDAW_GUI
#include "MainWindow.h"
#include "TimelineView.h"
#include "TimelineScene.h"
#include "ClipItem.h"
#include "TrackHeaderWidget.h"
#include "PianoRollWidget.h"
#include "NoteGridWidget.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"
#include <QGraphicsView>
#include <QScrollBar>
#include <QStackedWidget>
#include <QGraphicsScene>
#endif

#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"

namespace HDAW {

#ifdef HDAW_GUI

GuiInspector::GuiInspector(MainWindow* mw) : mw(mw) {}

bool GuiInspector::isAvailable() const { return mw != nullptr; }

QJsonObject GuiInspector::snapshot() const
{
    QJsonObject obj;
    obj["available"] = isAvailable();
    if (!isAvailable())
    {
        obj["reason"] = "no_gui";
        obj["hint"] = "Launch with --gui to enable GUI inspection tools";
        return obj;
    }

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    double pps = scene ? scene->getPixelsPerSecond() : 0.0;
    obj["pixelsPerSecond"] = pps;

    QJsonObject timeline;
    if (scene)
    {
        auto sr = scene->sceneRect();
        timeline["sceneWidth"] = sr.width();
        timeline["sceneHeight"] = sr.height();
        timeline["trackCount"] = scene->getTrackCount();
    }
    auto clips = clipGeometry();
    timeline["clipCount"] = clips.size();
    obj["timeline"] = timeline;

    obj["selection"] = selectionState();
    obj["panel"] = panelState();
    obj["tracks"] = trackLayout();
    obj["clips"] = clips;
    return obj;
}

QJsonArray GuiInspector::clipGeometry(int clipId) const
{
    QJsonArray arr;
    if (!isAvailable()) return arr;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) return arr;

    for (auto* item : scene->items())
    {
        auto* clip = dynamic_cast<ClipItem*>(item);
        if (!clip) continue;

        auto tree = clip->getClipTree();
        int id = static_cast<int>(tree.getProperty(IDs::clipID, 0));
        if (clipId >= 0 && id != clipId) continue;

        int trackIdx = scene->trackIndexAtY(clip->pos().y());
        QJsonObject c;
        c["clipId"] = id;
        c["trackIndex"] = trackIdx;
        c["x"] = clip->pos().x();
        c["y"] = clip->pos().y();
        c["width"] = clip->boundingRect().width();
        c["height"] = clip->boundingRect().height();
        c["selected"] = clip->isSelected();
        c["visible"] = clip->isVisible();
        c["type"] = QString::fromStdString(
            tree.getProperty(IDs::clipType).toString().toStdString());
        c["name"] = QString::fromStdString(
            tree.getProperty(IDs::name).toString().toStdString());
        arr.append(c);
    }
    return arr;
}

QJsonArray GuiInspector::trackLayout() const
{
    QJsonArray arr;
    if (!isAvailable()) return arr;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) return arr;

    int count = scene->getTrackCount();
    for (int i = 0; i < count; ++i)
    {
        QJsonObject t;
        t["index"] = i;
        t["y"] = scene->getTrackY(i);
        t["height"] = scene->getTrackHeight(i);

        auto trackList = mw->getEngine().getProjectModel().getTrackListTree();
        if (i < trackList.getNumChildren())
        {
            auto trackTree = trackList.getChild(i);
            t["name"] = QString::fromStdString(
                trackTree.getProperty(IDs::name).toString().toStdString());
            t["muted"] = trackTree.getProperty(IDs::isMuted, false);
            t["soloed"] = trackTree.getProperty(IDs::isSoloed, false);
            t["armed"] = trackTree.getProperty(IDs::isArm, false);
            t["clipCount"] = trackTree.getChildWithName(IDs::CLIP_LIST).getNumChildren();
        }
        arr.append(t);
    }
    return arr;
}

QJsonObject GuiInspector::selectionState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;

    QJsonArray selectedClips;
    if (scene)
    {
        for (auto* item : scene->selectedItems())
        {
            auto* clip = dynamic_cast<ClipItem*>(item);
            if (clip)
                selectedClips.append(
                    static_cast<int>(clip->getClipTree().getProperty(IDs::clipID, 0)));
        }
    }
    obj["selectedClips"] = selectedClips;
    obj["selectedTrack"] = tv ? tv->property("selectedTrack").toInt() : -1;
    return obj;
}

QJsonObject GuiInspector::scrollState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* tv = mw->findChild<TimelineView*>();
    if (!tv) return obj;

    auto* gv = tv->findChild<QGraphicsView*>();
    if (gv)
    {
        obj["timelineScrollX"] = gv->horizontalScrollBar()->value();
        obj["timelineScrollY"] = gv->verticalScrollBar()->value();
    }

    auto* scene = tv->getScene();
    if (scene)
        obj["pixelsPerSecond"] = scene->getPixelsPerSecond();

    return obj;
}

QJsonObject GuiInspector::panelState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* stack = mw->findChild<QStackedWidget*>();
    if (!stack) return obj;

    int idx = stack->currentIndex();
    static const char* tabNames[] = {
        "Mixer", "PianoRoll", "FxChain", "Automation",
        "AudioEditor", "StepSequencer", "Modulation"
    };
    obj["activeTabIndex"] = idx;
    obj["activeTab"] = (idx >= 0 && idx < 7) ? tabNames[idx] : "Unknown";
    obj["tabCount"] = stack->count();

    QJsonArray tabs;
    for (int i = 0; i < stack->count() && i < 7; ++i)
        tabs.append(QString(tabNames[i]));
    obj["tabs"] = tabs;
    return obj;
}

QJsonObject GuiInspector::pianoRollState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* pr = mw->findChild<PianoRollWidget*>();
    if (!pr) { obj["loaded"] = false; return obj; }

    auto* noteGrid = pr->findChild<NoteGridWidget*>();
    obj["loaded"] = (noteGrid != nullptr);

    auto trackList = mw->getEngine().getProjectModel().getTrackListTree();
    QJsonArray notes;

    for (int ti = 0; ti < trackList.getNumChildren(); ++ti)
    {
        auto trackTree = trackList.getChild(ti);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
        {
            auto clipTree = clipList.getChild(ci);
            if (clipTree.getProperty(IDs::clipType).toString() != juce::String("midi")) continue;
            auto noteList = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int ni = 0; ni < noteList.getNumChildren(); ++ni)
            {
                auto noteTree = noteList.getChild(ni);
                QJsonObject n;
                n["noteId"] = static_cast<int>(noteTree.getProperty(IDs::noteID, 0));
                n["pitch"] = static_cast<int>(noteTree.getProperty(IDs::noteNumber, 60));
                n["velocity"] = static_cast<int>(noteTree.getProperty(IDs::velocity, 100));
                n["startBeat"] = static_cast<double>(noteTree.getProperty(IDs::startBeat, 0.0));
                n["durationBeats"] = static_cast<double>(noteTree.getProperty(IDs::durationBeats, 1.0));
                notes.append(n);
            }
        }
    }
    obj["notes"] = notes;
    return obj;
}

QJsonObject GuiInspector::hitTest(double sceneX, double sceneY) const
{
    QJsonObject obj;
    if (!isAvailable()) { obj["hit"] = false; return obj; }

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) { obj["hit"] = false; return obj; }

    auto* item = scene->itemAt(sceneX, sceneY, QTransform());
    auto* clip = dynamic_cast<ClipItem*>(item);
    if (clip)
    {
        auto tree = clip->getClipTree();
        obj["hit"] = true;
        obj["type"] = "clip";
        obj["clipId"] = static_cast<int>(tree.getProperty(IDs::clipID, 0));
        obj["trackIndex"] = scene->trackIndexAtY(clip->pos().y());
        obj["name"] = QString::fromStdString(
            tree.getProperty(IDs::name).toString().toStdString());
    }
    else
    {
        obj["hit"] = false;
    }
    return obj;
}

#else // !HDAW_GUI — model-state inspection for headless/browser/Electron builds

GuiInspector::GuiInspector(MainWindow*) {}
bool GuiInspector::isAvailable() const { return false; }

QJsonObject GuiInspector::snapshot() const
{
    return {{"available", false}, {"reason", "no_gui"},
            {"hint", "Launch with --gui to enable widget-level GUI inspection. "
                     "Model-state inspection is available via read.* MCP tools."}};
}

QJsonArray GuiInspector::clipGeometry(int) const { return {}; }
QJsonArray GuiInspector::trackLayout() const { return {}; }
QJsonObject GuiInspector::selectionState() const { return {}; }
QJsonObject GuiInspector::scrollState() const { return {}; }
QJsonObject GuiInspector::panelState() const { return {}; }
QJsonObject GuiInspector::pianoRollState() const { return {{"loaded", false}}; }
QJsonObject GuiInspector::hitTest(double, double) const { return {{"hit", false}}; }

#endif // HDAW_GUI

} // namespace HDAW
