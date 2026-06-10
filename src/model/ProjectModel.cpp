#include "ProjectModel.h"
#include <atomic>

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

static juce::ValueTree createVolumeAutomation()
{
    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, "Volume", nullptr);
    autoTree.setProperty(IDs::paramID, 1, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);

    juce::ValueTree pointList(IDs::POINT_LIST);
    pointList.addChild(createPoint(0.0, 1.0), -1, nullptr);
    pointList.addChild(createPoint(16.0, 1.0), -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);

    return autoTree;
}

static juce::ValueTree createAutomationList()
{
    juce::ValueTree list(IDs::AUTOMATION_LIST);
    list.addChild(createVolumeAutomation(), -1, nullptr);
    return list;
}

static std::atomic<int> nextClipID{1};

int ProjectModel::allocateClipID()
{
    return nextClipID.fetch_add(1, std::memory_order_relaxed);
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

static juce::ValueTree createAudioClip(juce::String name, double start, double dur, juce::String file)
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
    return clip;
}

static juce::ValueTree createMidiNote(int note, float vel, double start, double dur)
{
    juce::ValueTree noteNode(IDs::MIDI_NOTE);
    noteNode.setProperty(IDs::noteNumber, note, nullptr);
    noteNode.setProperty(IDs::velocity, vel, nullptr);
    noteNode.setProperty(IDs::startBeat, start, nullptr);
    noteNode.setProperty(IDs::durationBeats, dur, nullptr);
    return noteNode;
}

static juce::ValueTree createMidiClip(juce::String name, double start, double dur)
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

    juce::ValueTree midiNotes(IDs::MIDI_NOTE_LIST);
    midiNotes.addChild(createMidiNote(60, 0.8f, 0.0, 1.0), -1, nullptr);
    midiNotes.addChild(createMidiNote(64, 0.7f, 1.0, 0.5), -1, nullptr);
    midiNotes.addChild(createMidiNote(67, 0.9f, 1.5, 1.5), -1, nullptr);
    midiNotes.addChild(createMidiNote(72, 0.6f, 3.0, 0.25), -1, nullptr);
    midiNotes.addChild(createMidiNote(71, 0.5f, 3.25, 0.25), -1, nullptr);
    midiNotes.addChild(createMidiNote(69, 0.7f, 3.5, 0.5), -1, nullptr);
    clip.addChild(midiNotes, -1, nullptr);

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
    projectTree.addChild(transport, -1, nullptr);

    juce::ValueTree trackList(IDs::TRACK_LIST);
    projectTree.addChild(trackList, -1, nullptr);

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
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        track1.addChild(clipList, -1, nullptr);
        track1.addChild(createFXChain(), -1, nullptr);
        track1.addChild(createAutomationList(), -1, nullptr);
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
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        clipList.addChild(createMidiClip("Melody", 0.0, 4.0), -1, nullptr);
        clipList.addChild(createMidiClip("Chords", 4.0, 4.0), -1, nullptr);
        track2.addChild(clipList, -1, nullptr);
        track2.addChild(createFXChain(), -1, nullptr);
        track2.addChild(createAutomationList(), -1, nullptr);
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
    {
        juce::ValueTree clipList(IDs::CLIP_LIST);
        track3.addChild(clipList, -1, nullptr);
        track3.addChild(createFXChain(), -1, nullptr);
        track3.addChild(createAutomationList(), -1, nullptr);
    }
    trackList.addChild(track3, -1, nullptr);
}
