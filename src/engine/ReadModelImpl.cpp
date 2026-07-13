#include "ReadModelImpl.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "Track.h"
#include "../model/ProjectModel.h"

ReadModelImpl::ReadModelImpl(ProjectModel& model)
    : model_(model) {}

ProjectSnapshot ReadModelImpl::snapshot() const
{
    ProjectSnapshot snap;
    snap.name = model_.getTree().getProperty(IDs::name, "Untitled").toString().toStdString();
    snap.transport = getTransport();
    snap.scaleRoot = model_.getScaleRoot();
    snap.scaleMode = model_.getScaleMode();

    auto trackList = model_.getTrackListTree();
    const int numTracks = trackList.getNumChildren();
    snap.tracks.reserve(numTracks);

    for (int t = 0; t < numTracks; ++t) {
        auto trackTree = trackList.getChild(t);
        TrackSnapshot ts;
        ts.index = t;
        ts.name = trackTree.getProperty(IDs::name, "Track").toString().toStdString();
        ts.color = static_cast<int>(trackTree.getProperty(IDs::color, 0));
        ts.volume = trackTree.getProperty(IDs::volume, 1.0);
        ts.pan = trackTree.getProperty(IDs::pan, 0.0);
        ts.muted = trackTree.getProperty(IDs::isMuted, false);
        ts.soloed = trackTree.getProperty(IDs::isSoloed, false);
        ts.armed = trackTree.getProperty(IDs::isArm, false);
        ts.inputMonitor = trackTree.getProperty(IDs::inputMonitor, false);
        ts.height = trackTree.getProperty(IDs::trackHeight, 80.0);
        ts.midiChannel = trackTree.getProperty(IDs::midiChannel, 1);

        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        ts.clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
        snap.tracks.push_back(ts);

        if (!clipList.isValid())
            continue;

        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clipTree = clipList.getChild(c);
            ClipSnapshot cs;
            cs.clipId = static_cast<int>(clipTree.getProperty(IDs::clipID, 0));
            cs.trackIndex = t;
            cs.name = clipTree.getProperty(IDs::name, "").toString().toStdString();
            cs.sourceFile = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
            cs.startBeat = clipTree.getProperty(IDs::startTime, 0.0);
            cs.durationBeats = clipTree.getProperty(IDs::duration, 0.0);
            cs.offset = clipTree.getProperty(IDs::offset, 0.0);
            cs.gain = clipTree.getProperty(IDs::gain, 1.0);
            cs.fadeIn = clipTree.getProperty(IDs::fadeIn, 0.0);
            cs.fadeOut = clipTree.getProperty(IDs::fadeOut, 0.0);
            cs.looping = clipTree.getProperty(IDs::looping, false);
            cs.isMidi = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
            cs.sourceBpm = clipTree.getProperty(IDs::sourceBpm, 0.0);
            cs.stretchMode = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
            cs.stretchRatio = clipTree.getProperty(IDs::stretchRatio, 1.0);
            cs.sourceDuration = clipTree.getProperty(IDs::sourceDuration, 0.0);
            snap.clips.push_back(cs);
        }
    }

    return snap;
}

int ReadModelImpl::getTrackCount() const
{
    return model_.getTrackListTree().getNumChildren();
}

TrackSnapshot ReadModelImpl::getTrack(int index) const
{
    auto trackList = model_.getTrackListTree();
    if (index < 0 || index >= trackList.getNumChildren())
    {
        TrackSnapshot ts;
        ts.index = -1;
        return ts;
    }

    auto trackTree = trackList.getChild(index);
    TrackSnapshot ts;
    ts.index = index;
    ts.name = trackTree.getProperty(IDs::name, "Track").toString().toStdString();
    ts.color = static_cast<int>(trackTree.getProperty(IDs::color, 0));
    ts.volume = trackTree.getProperty(IDs::volume, 1.0);
    ts.pan = trackTree.getProperty(IDs::pan, 0.0);
    ts.muted = trackTree.getProperty(IDs::isMuted, false);
    ts.soloed = trackTree.getProperty(IDs::isSoloed, false);
    ts.armed = trackTree.getProperty(IDs::isArm, false);
    ts.inputMonitor = trackTree.getProperty(IDs::inputMonitor, false);
    ts.height = trackTree.getProperty(IDs::trackHeight, 80.0);
    ts.midiChannel = trackTree.getProperty(IDs::midiChannel, 1);

    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    ts.clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
    return ts;
}

ClipSnapshot ReadModelImpl::getClip(int clipId) const
{
    auto trackList = model_.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
            continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clipTree = clipList.getChild(c);
            if (static_cast<int>(clipTree.getProperty(IDs::clipID, 0)) == clipId) {
                ClipSnapshot cs;
                cs.clipId = clipId;
                cs.trackIndex = t;
                cs.name = clipTree.getProperty(IDs::name, "").toString().toStdString();
                cs.sourceFile = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
                cs.startBeat = clipTree.getProperty(IDs::startTime, 0.0);
                cs.durationBeats = clipTree.getProperty(IDs::duration, 0.0);
                cs.offset = clipTree.getProperty(IDs::offset, 0.0);
                cs.gain = clipTree.getProperty(IDs::gain, 1.0);
                cs.fadeIn = clipTree.getProperty(IDs::fadeIn, 0.0);
                cs.fadeOut = clipTree.getProperty(IDs::fadeOut, 0.0);
                cs.looping = clipTree.getProperty(IDs::looping, false);
                cs.isMidi = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
                cs.sourceBpm = clipTree.getProperty(IDs::sourceBpm, 0.0);
                cs.stretchMode = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
                cs.stretchRatio = clipTree.getProperty(IDs::stretchRatio, 1.0);
                cs.sourceDuration = clipTree.getProperty(IDs::sourceDuration, 0.0);
                return cs;
            }
        }
    }
    return {};
}

std::vector<NoteSnapshot> ReadModelImpl::getNotes(int clipId) const
{
    auto trackList = model_.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
            continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clipTree = clipList.getChild(c);
            if (static_cast<int>(clipTree.getProperty(IDs::clipID, 0)) != clipId)
                continue;

            std::vector<NoteSnapshot> notes;
            auto noteList = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!noteList.isValid())
                return notes;

            notes.reserve(noteList.getNumChildren());
            for (int n = 0; n < noteList.getNumChildren(); ++n) {
                auto noteTree = noteList.getChild(n);
                NoteSnapshot ns;
                ns.noteId = static_cast<int>(noteTree.getProperty(IDs::noteID, 0));
                ns.pitch = static_cast<int>(noteTree.getProperty(IDs::noteNumber, 0));
                ns.velocity = static_cast<int>(
                    static_cast<double>(noteTree.getProperty(IDs::velocity, 0)) * 127.0 + 0.5);
                ns.startBeat = noteTree.getProperty(IDs::startBeat, 0.0);
                ns.durationBeats = noteTree.getProperty(IDs::durationBeats, 0.0);
                notes.push_back(ns);
            }
            return notes;
        }
    }
    return {};
}

TransportSnapshot ReadModelImpl::getTransport() const
{
    TransportSnapshot ts;
    ts.bpm = model_.getTree().getProperty(IDs::tempo, 120.0);
    auto transport = model_.getTransportTree();
    if (transport.isValid()) {
        ts.isPlaying = transport.getProperty(IDs::isPlaying, false);
        ts.isLooping = transport.getProperty(IDs::isLooping, false);
        ts.loopStart = transport.getProperty(IDs::loopStart, 0.0);
        ts.loopEnd = transport.getProperty(IDs::loopEnd, 8.0);
        ts.currentTimeSeconds = transport.getProperty(IDs::position, 0.0);
    }
    return ts;
}

int ReadModelImpl::getScaleRoot() const
{
    return model_.getScaleRoot();
}

int ReadModelImpl::getScaleMode() const
{
    return model_.getScaleMode();
}

std::vector<FxSlotSnapshot> ReadModelImpl::getFxSlots(int trackIndex) const
{
    std::vector<FxSlotSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;

    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
        return result;

    for (int i = 0; i < fxChain.getNumChildren(); ++i)
    {
        auto slot = fxChain.getChild(i);
        FxSlotSnapshot s;
        s.slotIndex = i;
        s.fxType = slot.getProperty(IDs::fxType, "").toString().toStdString();
        s.pluginId = slot.getProperty(IDs::pluginID, "").toString().toStdString();
        s.pluginName = slot.getProperty(IDs::name, "").toString().toStdString();
        s.bypassed = slot.getProperty(IDs::bypassed, false);
        s.paramCount = slot.getNumChildren();
        result.push_back(s);
    }
    return result;
}

std::vector<AutomationLaneSnapshot> ReadModelImpl::getAutomationLanes(int trackIndex) const
{
    std::vector<AutomationLaneSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;

    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
        return result;

    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        auto lane = autoList.getChild(i);
        AutomationLaneSnapshot l;
        l.laneIndex = i;
        l.name = lane.getProperty(IDs::name, "").toString().toStdString();
        l.paramID = static_cast<int>(lane.getProperty(IDs::paramID, 0));
        l.enabled = lane.getProperty(IDs::automationEnabled, false);
        result.push_back(l);
    }
    return result;
}

std::vector<AutomationPointSnapshot> ReadModelImpl::getAutomationPoints(
    int trackIndex, const std::string& laneName) const
{
    std::vector<AutomationPointSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;

    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
        return result;

    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        auto lane = autoList.getChild(i);
        if (lane.getProperty(IDs::name, "").toString().toStdString() != laneName)
            continue;

        auto pointList = lane.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid())
            return result;

        for (int p = 0; p < pointList.getNumChildren(); ++p)
        {
            auto pt = pointList.getChild(p);
            AutomationPointSnapshot aps;
            aps.time = pt.getProperty(IDs::startTime, 0.0);
            aps.value = static_cast<float>(
                static_cast<double>(pt.getProperty(IDs::gain, 0.0)));
            result.push_back(aps);
        }
        break;
    }
    return result;
}

std::vector<MarkerSnapshot> ReadModelImpl::getMarkers() const
{
    std::vector<MarkerSnapshot> result;
    auto markerList = model_.getTree().getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid())
        return result;

    for (int i = 0; i < markerList.getNumChildren(); ++i)
    {
        auto marker = markerList.getChild(i);
        MarkerSnapshot ms;
        ms.index = i;
        ms.time = marker.getProperty(IDs::markerTime, 0.0);
        ms.name = marker.getProperty(IDs::markerName, "").toString().toStdString();
        ms.color = static_cast<int>(marker.getProperty(IDs::markerColor, 0));
        result.push_back(ms);
    }
    return result;
}

std::vector<TempoPointSnapshot> ReadModelImpl::getTempoPoints() const
{
    std::vector<TempoPointSnapshot> result;
    auto tempoList = model_.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid())
        return result;

    for (int i = 0; i < tempoList.getNumChildren(); ++i)
    {
        auto pt = tempoList.getChild(i);
        TempoPointSnapshot tps;
        tps.timeSeconds = pt.getProperty(IDs::startTime, 0.0);
        tps.bpm = pt.getProperty(IDs::tempo, 120.0);
        result.push_back(tps);
    }
    return result;
}

std::vector<AutomatableParamSnapshot> ReadModelImpl::getAutomatableParams(int trackIndex) const
{
    std::vector<AutomatableParamSnapshot> result;
    if (engine_ == nullptr) return result;
    auto* proc = engine_->getMainProcessor();
    if (proc == nullptr) return result;
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr) return result;

    // Walk the live FX chain. Each slot's getAutomatableParams() returns the
    // cached {name, index, automatable} triples built from the plugin's own
    // parameter metadata (TrackFXSlot::rebuildParamCache). The slot index is
    // preserved so callers can reconstruct the compound paramID
    // (100 + slotIndex*100 + paramIndex) used by the automation system.
    auto& fxChain = track->getFXChain();
    for (int si = 0; si < static_cast<int>(fxChain.size()); ++si)
    {
        auto& slot = fxChain[si];
        if (!slot || !slot->isPlugin() || slot->isBypassed())
            continue;

        const auto& params = slot->getAutomatableParams();
        for (const auto& p : params)
        {
            AutomatableParamSnapshot aps;
            aps.slotIndex = si;
            aps.paramIndex = p.index;
            aps.name = p.name.toStdString();
            aps.automatable = p.automatable;
            result.push_back(aps);
        }
    }
    return result;
}

MeterSnapshot ReadModelImpl::getTrackMeter(int trackIndex) const
{
    if (engine_ == nullptr) return {};
    auto* proc = engine_->getMainProcessor();
    if (proc == nullptr) return {};
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr) return {};
    MeterSnapshot ms;
    ms.leftLevel = track->getMeter().getLeftLevel();
    ms.rightLevel = track->getMeter().getRightLevel();
    return ms;
}

MeterSnapshot ReadModelImpl::getMasterMeter() const
{
    if (engine_ == nullptr) return {};
    auto* proc = engine_->getMainProcessor();
    if (proc == nullptr) return {};
    MeterSnapshot ms;
    ms.leftLevel = proc->getMasterMeter().getLeftLevel();
    ms.rightLevel = proc->getMasterMeter().getRightLevel();
    return ms;
}

bool ReadModelImpl::isDirty() const
{
    return model_.isDirty();
}
