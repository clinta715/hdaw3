#include "ProjectModel.h"
#include "../engine/PluginManager.h"
#include <atomic>
#include <algorithm>
#include <functional>

ProjectModel::ProjectModel()
    : projectTree(IDs::PROJECT)
{
    projectTree.addListener(this);
    createDefaultProject();
}

ProjectModel::~ProjectModel()
{
    projectTree.removeListener(this);
}

static juce::ValueTree createFXChain()
{
    juce::ValueTree chain(IDs::FX_CHAIN);
    return chain;
}

static juce::ValueTree createPoint(double time, double value)
{
    juce::ValueTree point(IDs::POINT);
    point.setProperty(IDs::startTime, time, nullptr);
    point.setProperty(IDs::gain, value, nullptr);
    return point;
}

static juce::ValueTree createAutomationLane(const juce::String& name, int paramID, double defaultVal)
{
    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, name, nullptr);
    autoTree.setProperty(IDs::paramID, paramID, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);

    juce::ValueTree pointList(IDs::POINT_LIST);
    pointList.addChild(createPoint(0.0, defaultVal), -1, nullptr);
    pointList.addChild(createPoint(16.0, defaultVal), -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);
    return autoTree;
}

juce::ValueTree ProjectModel::createTrackAutomationList()
{
    juce::ValueTree list(IDs::AUTOMATION_LIST);
    list.addChild(createAutomationLane("Volume", 1, 1.0), -1, nullptr);
    list.addChild(createAutomationLane("Pan", 2, 0.5), -1, nullptr);
    list.addChild(createAutomationLane("Mute", 3, 0.0), -1, nullptr);
    return list;
}

static std::atomic<int> nextClipID{1};

juce::ValueTree ProjectModel::getScaleInfoTree()
{
    auto tree = projectTree.getChildWithName(IDs::SCALE_INFO);
    if (!tree.isValid())
    {
        tree = juce::ValueTree(IDs::SCALE_INFO);
        tree.setProperty(IDs::scaleRoot, 0, nullptr);
        tree.setProperty(IDs::scaleMode, 0, nullptr);
        projectTree.addChild(tree, -1, nullptr);
    }
    return tree;
}

juce::ValueTree ProjectModel::getScaleInfoTree() const
{
    return projectTree.getChildWithName(IDs::SCALE_INFO);
}

int ProjectModel::getScaleRoot() const
{
    auto tree = getScaleInfoTree();
    return tree.isValid() ? static_cast<int>(tree.getProperty(IDs::scaleRoot, 0)) : 0;
}

int ProjectModel::getScaleMode() const
{
    auto tree = getScaleInfoTree();
    return tree.isValid() ? static_cast<int>(tree.getProperty(IDs::scaleMode, 0)) : 0;
}

void ProjectModel::setScaleRoot(int root)
{
    auto tree = getScaleInfoTree();
    if (tree.isValid())
        tree.setProperty(IDs::scaleRoot, root, &undoManager);
}

void ProjectModel::setScaleMode(int mode)
{
    auto tree = getScaleInfoTree();
    if (tree.isValid())
        tree.setProperty(IDs::scaleMode, mode, &undoManager);
}

int ProjectModel::allocateClipID()
{
    return nextClipID.fetch_add(1, std::memory_order_relaxed);
}

int ProjectModel::allocateNoteID()
{
    return nextNoteID.fetch_add(1, std::memory_order_relaxed);
}

void ProjectModel::resetNoteIDCounter()
{
    nextNoteID.store(1, std::memory_order_relaxed);
}

juce::uint32 ProjectModel::trackColorForIndex(int index)
{
    // Curated palette of distinct, dark-background-friendly hues (ARGB).
    static const juce::uint32 palette[] = {
        0xFF5B9BD5, // blue
        0xFFED7D31, // orange
        0xFF70AD47, // green
        0xFFFFC000, // gold
        0xFF7B68A6, // purple
        0xFF00B0A0, // teal
        0xFFE15F8C, // rose
        0xFF7F7F8E  // slate
    };
    constexpr int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    int i = index % n;
    if (i < 0) i += n;
    return palette[i];
}

void ProjectModel::resetClipIDCounter()
{
    nextClipID.store(1, std::memory_order_relaxed);
}

void ProjectModel::scanAndSyncClipIDs()
{
    int maxID = 0;
    auto trackList = projectTree.getChildWithName(IDs::TRACK_LIST);
    if (trackList.isValid())
    {
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid()) continue;
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                int id = clipList.getChild(c).getProperty(IDs::clipID, 0);
                if (id > maxID) maxID = id;
            }
        }
    }
    nextClipID.store(maxID + 1, std::memory_order_relaxed);
}

void ProjectModel::scanAndSyncNoteIDs()
{
    int maxExisting = 0;
    std::function<void(juce::ValueTree)> walk = [&](juce::ValueTree t) {
        if (t.hasType(IDs::MIDI_NOTE))
        {
            if (t.hasProperty(IDs::noteID))
            {
                maxExisting = std::max(maxExisting, static_cast<int>(t.getProperty(IDs::noteID)));
            }
            else
            {
                int id = nextNoteID.fetch_add(1, std::memory_order_relaxed);
                t.setProperty(IDs::noteID, id, nullptr);
                maxExisting = std::max(maxExisting, id);
            }
            return;
        }
        for (int i = 0; i < t.getNumChildren(); ++i)
            walk(t.getChild(i));
    };
    walk(projectTree);

    int cur = nextNoteID.load(std::memory_order_relaxed);
    while (cur <= maxExisting && !nextNoteID.compare_exchange_weak(cur, maxExisting + 1)) {}
}

juce::ValueTree ProjectModel::createAudioClip(juce::String name, double start, double dur, juce::String file)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), nullptr);
    clip.setProperty(IDs::name, name, nullptr);
    clip.setProperty(IDs::startTime, start, nullptr);
    clip.setProperty(IDs::duration, dur, nullptr);
    clip.setProperty(IDs::offset, 0.0, nullptr);
    clip.setProperty(IDs::clipType, "audio", nullptr);
    clip.setProperty(IDs::sourceFile, file, nullptr);
    clip.setProperty(IDs::gain, 1.0, nullptr);
    clip.setProperty(IDs::fadeIn, 0.0, nullptr);
    clip.setProperty(IDs::fadeOut, 0.0, nullptr);
    clip.setProperty(IDs::looping, false, nullptr);
    clip.setProperty(IDs::color, static_cast<int>(0xFF4488CC), nullptr);
    clip.setProperty(IDs::sourceBpm, 0.0, nullptr);
    clip.setProperty(IDs::stretchMode, 0, nullptr);
    clip.setProperty(IDs::stretchRatio, 1.0, nullptr);
    clip.setProperty(IDs::sourceDuration, dur, nullptr);
    return clip;
}

juce::ValueTree ProjectModel::createMidiNote(int note, float vel, double start, double dur)
{
    juce::ValueTree noteNode(IDs::MIDI_NOTE);
    noteNode.setProperty(IDs::noteID, ProjectModel::allocateNoteID(), nullptr);
    noteNode.setProperty(IDs::noteNumber, note, nullptr);
    noteNode.setProperty(IDs::velocity, vel, nullptr);
    noteNode.setProperty(IDs::startBeat, start, nullptr);
    noteNode.setProperty(IDs::durationBeats, dur, nullptr);
    return noteNode;
}

juce::ValueTree ProjectModel::createMidiClipEmpty(juce::String name, double start, double dur)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), nullptr);
    clip.setProperty(IDs::name, name, nullptr);
    clip.setProperty(IDs::startTime, start, nullptr);
    clip.setProperty(IDs::duration, dur, nullptr);
    clip.setProperty(IDs::offset, 0.0, nullptr);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    clip.setProperty(IDs::gain, 1.0, nullptr);
    clip.setProperty(IDs::fadeIn, 0.0, nullptr);
    clip.setProperty(IDs::fadeOut, 0.0, nullptr);
    clip.setProperty(IDs::looping, false, nullptr);
    clip.setProperty(IDs::color, static_cast<int>(0xFFCC8844), nullptr);
    clip.addChild(juce::ValueTree(IDs::MIDI_NOTE_LIST), -1, nullptr);
    return clip;
}

juce::ValueTree ProjectModel::getTrackOfClip(const juce::ValueTree& clip)
{
    if (!clip.isValid() || !clip.hasType(IDs::CLIP)) return {};
    auto clipList = clip.getParent();
    if (!clipList.isValid()) return {};
    return clipList.getParent();
}

static juce::ValueTree createMidiClip(juce::String name, double start, double dur)
{
    juce::ValueTree clip = ProjectModel::createMidiClipEmpty(name, start, dur);
    auto midiNotes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    midiNotes.addChild(ProjectModel::createMidiNote(60, 0.8f, 0.0, 1.0), -1, nullptr);
    midiNotes.addChild(ProjectModel::createMidiNote(64, 0.7f, 1.0, 0.5), -1, nullptr);
    midiNotes.addChild(ProjectModel::createMidiNote(67, 0.9f, 1.5, 1.5), -1, nullptr);
    midiNotes.addChild(ProjectModel::createMidiNote(72, 0.6f, 3.0, 0.25), -1, nullptr);
    midiNotes.addChild(ProjectModel::createMidiNote(71, 0.5f, 3.25, 0.25), -1, nullptr);
    midiNotes.addChild(ProjectModel::createMidiNote(69, 0.7f, 3.5, 0.5), -1, nullptr);
    return clip;
}

static juce::ValueTree createTempoPointList()
{
    juce::ValueTree list(IDs::TEMPO_POINT_LIST);
    juce::ValueTree pt(IDs::TEMPO_POINT);
    pt.setProperty(IDs::startTime, 0.0, nullptr);
    pt.setProperty(IDs::tempo, 120.0, nullptr);
    list.addChild(pt, -1, nullptr);
    return list;
}

void ProjectModel::createDefaultProject()
{
    resetClipIDCounter();
    projectTree.removeAllChildren(&undoManager);
    projectTree.removeAllProperties(&undoManager);

    undoManager.clearUndoHistory();

    projectTree.setProperty(IDs::name, "New Project", &undoManager);
    projectTree.setProperty(IDs::tempo, 120.0, &undoManager);

    juce::ValueTree transport(IDs::TRANSPORT);
    transport.setProperty(IDs::position, 0.0, nullptr);
    transport.setProperty(IDs::isPlaying, false, nullptr);
    transport.setProperty(IDs::loopStart, 0.0, nullptr);
    transport.setProperty(IDs::loopEnd, 8.0, nullptr);
    transport.setProperty(IDs::isLooping, false, nullptr);
    transport.setProperty(IDs::timeSigNumerator, 4, nullptr);
    transport.setProperty(IDs::timeSigDenominator, 4, nullptr);
    projectTree.addChild(transport, -1, nullptr);

    juce::ValueTree trackList(IDs::TRACK_LIST);
    projectTree.addChild(trackList, -1, nullptr);

    // Scale info (root=C, mode=Major)
    {
        juce::ValueTree scaleInfo(IDs::SCALE_INFO);
        scaleInfo.setProperty(IDs::scaleRoot, 0, nullptr);
        scaleInfo.setProperty(IDs::scaleMode, 0, nullptr);
        projectTree.addChild(scaleInfo, -1, nullptr);
    }

    // Tempo track
    projectTree.addChild(createTempoPointList(), -1, nullptr);

    // Routing graph
    juce::ValueTree routingGraph(IDs::ROUTING_GRAPH);
    juce::ValueTree busList(IDs::BUS_LIST);

    juce::ValueTree masterBus(IDs::BUS);
    masterBus.setProperty(IDs::name, "Master", nullptr);
    masterBus.setProperty(IDs::busID, 0, nullptr);
    masterBus.setProperty(IDs::busType, "master", nullptr);
    masterBus.setProperty(IDs::busTarget, -1, nullptr);
    masterBus.setProperty(IDs::fxType, "none", nullptr);
    busList.addChild(masterBus, -1, nullptr);

    juce::ValueTree fxBus(IDs::BUS);
    fxBus.setProperty(IDs::name, "Reverb", nullptr);
    fxBus.setProperty(IDs::busID, 1, nullptr);
    fxBus.setProperty(IDs::busType, "fx", nullptr);
    fxBus.setProperty(IDs::busTarget, 0, nullptr);
    fxBus.setProperty(IDs::fxType, "reverb", nullptr);
    busList.addChild(fxBus, -1, nullptr);

    routingGraph.addChild(busList, -1, nullptr);
    projectTree.addChild(routingGraph, -1, nullptr);

    // Track 1 — audio
    juce::ValueTree track1(IDs::TRACK);
    track1.setProperty(IDs::name, "Track 1", nullptr);
    track1.setProperty(IDs::volume, 1.0, nullptr);
    track1.setProperty(IDs::pan, 0.0, nullptr);
    track1.setProperty(IDs::isMuted, false, nullptr);
    track1.setProperty(IDs::isSoloed, false, nullptr);
    track1.setProperty(IDs::parentBus, 0, nullptr);
    track1.setProperty(IDs::color, static_cast<int>(trackColorForIndex(0)), nullptr);
    track1.setProperty(IDs::midiChannel, 1, nullptr);
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        track1.addChild(clipList, -1, nullptr);
        track1.addChild(createFXChain(), -1, nullptr);
        track1.addChild(createTrackAutomationList(), -1, nullptr);
    }
    trackList.addChild(track1, -1, nullptr);

    // Track 2 — MIDI
    juce::ValueTree track2(IDs::TRACK);
    track2.setProperty(IDs::name, "Synth", nullptr);
    track2.setProperty(IDs::volume, 0.85, nullptr);
    track2.setProperty(IDs::pan, 0.0, nullptr);
    track2.setProperty(IDs::isMuted, false, nullptr);
    track2.setProperty(IDs::isSoloed, false, nullptr);
    track2.setProperty(IDs::parentBus, 0, nullptr);
    track2.setProperty(IDs::color, static_cast<int>(trackColorForIndex(1)), nullptr);
    track2.setProperty(IDs::midiChannel, 1, nullptr); // Default MIDI channel 1
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        clipList.addChild(createMidiClip("Melody", 0.0, 4.0), -1, nullptr);
        clipList.addChild(createMidiClip("Chords", 4.0, 4.0), -1, nullptr);
        track2.addChild(clipList, -1, nullptr);
        track2.addChild(createFXChain(), -1, nullptr);
        track2.addChild(createTrackAutomationList(), -1, nullptr);
    }
    trackList.addChild(track2, -1, nullptr);

    // Track 3 — audio
    juce::ValueTree track3(IDs::TRACK);
    track3.setProperty(IDs::name, "Vocals", nullptr);
    track3.setProperty(IDs::volume, 0.9, nullptr);
    track3.setProperty(IDs::pan, 0.0, nullptr);
    track3.setProperty(IDs::isMuted, false, nullptr);
    track3.setProperty(IDs::isSoloed, false, nullptr);
    track3.setProperty(IDs::parentBus, 0, nullptr);
    track3.setProperty(IDs::color, static_cast<int>(trackColorForIndex(2)), nullptr);
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        track3.addChild(clipList, -1, nullptr);
        track3.addChild(createFXChain(), -1, nullptr);
        track3.addChild(createTrackAutomationList(), -1, nullptr);
    }
    trackList.addChild(track3, -1, nullptr);
}

int ProjectModel::addFxSlot(int trackIdx, const std::string& type, int pos,
                            const std::string& pluginID)
{
    auto tl = getTrackListTree();
    if (trackIdx < 0 || trackIdx >= tl.getNumChildren()) return -1;
    auto track = tl.getChild(trackIdx);
    auto fxChainTree = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChainTree.isValid())
    {
        fxChainTree = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChainTree, -1, &undoManager);
    }
    const int n = fxChainTree.getNumChildren();
    int insertIdx = (pos < 0 || pos > n) ? n : pos;
    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(type), &undoManager);
    if (type == "plugin" && !pluginID.empty())
    {
        slot.setProperty(IDs::pluginID, juce::String(pluginID), &undoManager);
        slot.setProperty(IDs::pluginFormat, juce::String(resolvePluginFormat(pluginID)),
                         &undoManager);
    }
    slot.setProperty(IDs::bypassed, false, &undoManager);
    fxChainTree.addChild(slot, insertIdx, &undoManager);
    return insertIdx;
}

juce::ValueTree ProjectModel::ensureGainEnvelope(juce::ValueTree clip)
{
    if (!clip.isValid()) return {};
    auto env = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!env.isValid())
    {
        env = juce::ValueTree(IDs::GAIN_ENVELOPE);
        clip.addChild(env, -1, nullptr);
    }
    return env;
}

juce::ValueTree ProjectModel::addGainEnvelopePoint(juce::ValueTree envelope, double time, double gain, juce::UndoManager* um)
{
    if (!envelope.isValid() || !envelope.hasType(IDs::GAIN_ENVELOPE)) return {};
    juce::ValueTree point(IDs::GAIN_ENVELOPE_POINT);
    point.setProperty(IDs::pointTime, time, um);
    point.setProperty(IDs::pointGain, gain, um);
    // Insert sorted by time
    int insertIdx = 0;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        if (static_cast<double>(envelope.getChild(i).getProperty(IDs::pointTime)) < time)
            insertIdx = i + 1;
        else
            break;
    }
    envelope.addChild(point, insertIdx, um);
    return point;
}

std::vector<ProjectModel::GainEnvelopePoint> ProjectModel::getGainEnvelopePoints(const juce::ValueTree& envelope)
{
    std::vector<GainEnvelopePoint> result;
    if (!envelope.isValid()) return result;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        auto child = envelope.getChild(i);
        if (child.hasType(IDs::GAIN_ENVELOPE_POINT))
        {
            result.push_back({ child.getProperty(IDs::pointTime), child.getProperty(IDs::pointGain) });
        }
    }
    return result;
}

void ProjectModel::removeGainEnvelopePoint(juce::ValueTree envelope, int index, juce::UndoManager* um)
{
    if (!envelope.isValid() || index < 0 || index >= envelope.getNumChildren()) return;
    envelope.removeChild(index, um);
}

void ProjectModel::clearGainEnvelope(juce::ValueTree envelope, juce::UndoManager* um)
{
    if (!envelope.isValid()) return;
    envelope.removeAllChildren(um);
}

std::string ProjectModel::resolvePluginFormat(const std::string& pluginID) const
{
    if (pluginManager_ == nullptr) return {};
    juce::String jid(pluginID);
    for (const auto& p : pluginManager_->getPlugins())
    {
        if (p.fileOrIdentifier == jid || p.createIdentifierString() == jid)
            return p.pluginFormatName.toStdString();
    }
    return {};
}
