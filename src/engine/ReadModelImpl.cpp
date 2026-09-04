#include "ReadModelImpl.h"
#include "AudioEngine.h"
#include "AudioEngineCommands_Helpers.h"
#include "MainAudioProcessor.h"
#include "Track.h"
#include "TrackFXSlot.h"
#include "../model/ProjectModel.h"

#include <algorithm>
#include <map>
#include <set>

ReadModelImpl::ReadModelImpl(ProjectModel& model)
    : model_(model) {}

ClipSnapshot buildClipSnapshotFromTree(const juce::ValueTree& clipTree, double bpm)
{
    ClipSnapshot cs;
    cs.clipId        = static_cast<int>(clipTree.getProperty(IDs::clipID, 0));
    cs.name          = clipTree.getProperty(IDs::name, "").toString().toStdString();
    cs.sourceFile    = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
    double startTimeSec = clipTree.getProperty(IDs::startTime, 0.0);
    double durationSec  = clipTree.getProperty(IDs::duration, 0.0);
    cs.startBeat     = HDAW::secondsToBeats(startTimeSec, bpm);
    cs.durationBeats = HDAW::secondsToBeats(durationSec, bpm);
    cs.offset        = clipTree.getProperty(IDs::offset, 0.0);
    cs.gain          = clipTree.getProperty(IDs::gain, 1.0);
    cs.fadeIn        = clipTree.getProperty(IDs::fadeIn, 0.0);
    cs.fadeOut       = clipTree.getProperty(IDs::fadeOut, 0.0);
    cs.looping       = clipTree.getProperty(IDs::looping, false);
    cs.muted         = clipTree.getProperty(IDs::muted, false);
    cs.isMidi        = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
    cs.sourceBpm     = clipTree.getProperty(IDs::sourceBpm, 0.0);
    cs.stretchMode   = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
    cs.stretchRatio  = clipTree.getProperty(IDs::stretchRatio, 1.0);
    cs.sourceDuration= clipTree.getProperty(IDs::sourceDuration, 0.0);
    cs.isGhost       = static_cast<bool>(clipTree.getProperty(IDs::isGhost, 0));
    cs.ghostSourceId = static_cast<int>(clipTree.getProperty(IDs::ghostSourceId, -1));
    cs.sceneIndex    = static_cast<int>(clipTree.getProperty(IDs::sceneIndex, -1));

    // Take system
    auto takeList = clipTree.getChildWithName(IDs::TAKE_LIST);
    if (takeList.isValid())
    {
        cs.activeTake = static_cast<int>(clipTree.getProperty(IDs::activeTake, 0));
        cs.takeCount = takeList.getNumChildren();
        cs.takes.reserve(takeList.getNumChildren());
        for (int t = 0; t < takeList.getNumChildren(); ++t)
        {
            auto takeNode = takeList.getChild(t);
            TakeInfo ti;
            ti.name = takeNode.getProperty(IDs::name, "").toString().toStdString();
            ti.sourceFile = takeNode.getProperty(IDs::sourceFile, "").toString().toStdString();
            cs.takes.push_back(std::move(ti));
        }
    }
    else
    {
        cs.activeTake = 0;
        cs.takeCount = 0;
    }

    // Populate gain envelope from GAIN_ENVELOPE child tree
    auto envTree = clipTree.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envTree.isValid())
    {
        cs.gainEnvelope.reserve(envTree.getNumChildren());
        for (int i = 0; i < envTree.getNumChildren(); ++i)
        {
            auto pt = envTree.getChild(i);
            if (pt.hasType(IDs::GAIN_ENVELOPE_POINT))
            {
                ClipSnapshot::GainEnvelopePoint p;
                double tSec = pt.getProperty(IDs::pointTime, 0.0);
                p.time = HDAW::secondsToBeats(tSec, bpm);
                p.gain = pt.getProperty(IDs::pointGain, 1.0);
                cs.gainEnvelope.push_back(p);
            }
        }
    }

    // CLIP -> CLIP_LIST -> TRACK -> position within TRACK_LIST
    auto track = clipTree.getParent().getParent();
    cs.trackIndex = track.getParent().indexOf(track);
    return cs;
}

TrackSnapshot buildTrackSnapshotFromTree(const juce::ValueTree& trackTree)
{
    TrackSnapshot ts;
    ts.index         = trackTree.getParent().indexOf(trackTree);
    ts.name          = trackTree.getProperty(IDs::name, "Track").toString().toStdString();
    ts.color         = static_cast<int>(trackTree.getProperty(IDs::color, 0));
    ts.volume        = trackTree.getProperty(IDs::volume, 1.0);
    ts.pan           = trackTree.getProperty(IDs::pan, 0.0);
    ts.muted         = trackTree.getProperty(IDs::isMuted, false);
    ts.soloed        = trackTree.getProperty(IDs::isSoloed, false);
    ts.armed         = trackTree.getProperty(IDs::isArm, false);
    ts.inputMonitor  = trackTree.getProperty(IDs::inputMonitor, false);
    ts.height        = trackTree.getProperty(IDs::trackHeight, 80.0);
    ts.midiChannel   = trackTree.getProperty(IDs::midiChannel, 1);
    ts.trackType   = static_cast<int>(trackTree.getProperty(IDs::trackType, 0));
    ts.isCollapsed = trackTree.getProperty(IDs::isCollapsed, false);
    ts.isHidden    = trackTree.getProperty(IDs::isHidden, false);
    ts.parentId    = trackTree.getProperty(IDs::parentId, -1);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    ts.clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;

    bool effMuted = ts.muted;
    bool effSoloed = ts.soloed;
    auto trackList = trackTree.getParent();
    int current = ts.index;
    while (true)
    {
        int parentIdx = trackList.getChild(current).getProperty(IDs::parentId, -1);
        if (parentIdx < 0 || parentIdx >= trackList.getNumChildren()) break;
        effMuted  = effMuted  || static_cast<bool>(trackList.getChild(parentIdx).getProperty(IDs::isMuted, false));
        effSoloed = effSoloed || static_cast<bool>(trackList.getChild(parentIdx).getProperty(IDs::isSoloed, false));
        current = parentIdx;
    }
    ts.effectiveMuted = effMuted;
    ts.effectiveSoloed = effSoloed;

    return ts;
}

ProjectSnapshot ReadModelImpl::snapshot() const
{
    ProjectSnapshot snap;
    snap.name = model_.getTree().getProperty(IDs::name, "Untitled").toString().toStdString();
    snap.transport = getTransport();
    snap.scaleRoot = model_.getScaleRoot();
    snap.scaleMode = model_.getScaleMode();
    snap.masterGain = model_.getMasterGain();

    // Project-file metadata (defaults surface when loading legacy metadata-less files).
    snap.createdWithApp = model_.getTree().getProperty(IDs::createdWithApp, "unknown").toString().toStdString();
    snap.savedWithApp = model_.getTree().getProperty(IDs::savedWithApp, "unknown").toString().toStdString();
    snap.formatVersion = static_cast<int>(model_.getTree().getProperty(IDs::formatVersion, 0));

    auto trackList = model_.getTrackListTree();
    const int numTracks = trackList.getNumChildren();
    snap.tracks.reserve(numTracks);

    double bpm = model_.getTree().getProperty(IDs::tempo, 120.0);

    for (int t = 0; t < numTracks; ++t) {
        auto trackTree = trackList.getChild(t);
        snap.tracks.push_back(buildTrackSnapshotFromTree(trackTree));

        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
            continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
            snap.clips.push_back(buildClipSnapshotFromTree(clipList.getChild(c), bpm));
    }

    // Compute effective mute/solo by walking parent chain
    {
        std::map<int, int> childToParent;
        for (int t = 0; t < numTracks; ++t)
        {
            int parentId = trackList.getChild(t).getProperty(IDs::parentId, -1);
            if (parentId >= 0)
                childToParent[t] = parentId;
        }
        for (auto& ts : snap.tracks)
        {
            bool effMuted = ts.muted;
            bool effSoloed = ts.soloed;
            int current = ts.index;
            while (true)
            {
                auto it = childToParent.find(current);
                if (it == childToParent.end()) break;
                int parentIdx = it->second;
                if (parentIdx < 0 || parentIdx >= numTracks) break;
                const auto& parent = snap.tracks[parentIdx];
                effMuted = effMuted || parent.muted;
                effSoloed = effSoloed || parent.soloed;
                current = parentIdx;
            }
            ts.effectiveMuted = effMuted;
            ts.effectiveSoloed = effSoloed;
        }
    }

    // Session state
    auto sessionState = model_.getTree().getChildWithName(IDs::SESSION_STATE);
    if (sessionState.isValid()) {
        snap.launchedScene = static_cast<int>(sessionState.getProperty(IDs::launchedScene, -1));
        snap.sceneCount = static_cast<int>(sessionState.getProperty(IDs::sceneCount, 8));
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
    ts.isHidden = trackTree.getProperty(IDs::isHidden, false);

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
                double bpm = model_.getTree().getProperty(IDs::tempo, 120.0);
                double startTimeSec = clipTree.getProperty(IDs::startTime, 0.0);
                double durationSec = clipTree.getProperty(IDs::duration, 0.0);
                cs.startBeat = HDAW::secondsToBeats(startTimeSec, bpm);
                cs.durationBeats = HDAW::secondsToBeats(durationSec, bpm);
                cs.offset = clipTree.getProperty(IDs::offset, 0.0);
                cs.gain = clipTree.getProperty(IDs::gain, 1.0);
                cs.fadeIn = clipTree.getProperty(IDs::fadeIn, 0.0);
                cs.fadeOut = clipTree.getProperty(IDs::fadeOut, 0.0);
                cs.looping = clipTree.getProperty(IDs::looping, false);
                cs.muted = clipTree.getProperty(IDs::muted, false);
                cs.isMidi = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
                cs.sourceBpm = clipTree.getProperty(IDs::sourceBpm, 0.0);
                cs.stretchMode = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
                cs.stretchRatio = clipTree.getProperty(IDs::stretchRatio, 1.0);
                cs.sourceDuration = clipTree.getProperty(IDs::sourceDuration, 0.0);
                cs.isGhost = static_cast<bool>(clipTree.getProperty(IDs::isGhost, 0));
                cs.ghostSourceId = static_cast<int>(clipTree.getProperty(IDs::ghostSourceId, -1));
                cs.sceneIndex = static_cast<int>(clipTree.getProperty(IDs::sceneIndex, -1));

                // Take system
                auto takeList = clipTree.getChildWithName(IDs::TAKE_LIST);
                if (takeList.isValid())
                {
                    cs.activeTake = static_cast<int>(clipTree.getProperty(IDs::activeTake, 0));
                    cs.takeCount = takeList.getNumChildren();
                    cs.takes.reserve(takeList.getNumChildren());
                    for (int tk = 0; tk < takeList.getNumChildren(); ++tk)
                    {
                        auto takeNode = takeList.getChild(tk);
                        TakeInfo ti;
                        ti.name = takeNode.getProperty(IDs::name, "").toString().toStdString();
                        ti.sourceFile = takeNode.getProperty(IDs::sourceFile, "").toString().toStdString();
                        cs.takes.push_back(std::move(ti));
                    }
                }
                else
                {
                    cs.activeTake = 0;
                    cs.takeCount = 0;
                }

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
                ns.chance = static_cast<float>(noteTree.getProperty(IDs::chance, 1.0));
                ns.repeatCount = noteTree.getProperty(IDs::repeatCount, 0);
                ns.repeatRate = static_cast<float>(noteTree.getProperty(IDs::repeatRate, 0.25));
                ns.repeatCurve = static_cast<float>(noteTree.getProperty(IDs::repeatCurve, 0.0));
                ns.occurrence = noteTree.getProperty(IDs::occurrence, 0);
                ns.recurrence = noteTree.getProperty(IDs::recurrence, 0);
                ns.noteGain = static_cast<float>(noteTree.getProperty(IDs::noteGain, 1.0));
                ns.notePan = static_cast<float>(noteTree.getProperty(IDs::notePan, 0.0));
                ns.notePitch = static_cast<float>(noteTree.getProperty(IDs::notePitch, 0.0));
                ns.noteTimbre = static_cast<float>(noteTree.getProperty(IDs::noteTimbre, 0.5));
                ns.notePressure = static_cast<float>(noteTree.getProperty(IDs::notePressure, 0.0));
                notes.push_back(ns);
            }
            return notes;
        }
    }
    return {};
}

std::vector<CcPointSnapshot> ReadModelImpl::getCcPoints(int clipId, int controllerNumber) const
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

            std::vector<CcPointSnapshot> points;
            auto ccList = clipTree.getChildWithName(IDs::CC_LIST);
            if (!ccList.isValid())
                return points;

            for (int i = 0; i < ccList.getNumChildren(); ++i) {
                auto pt = ccList.getChild(i);
                if (static_cast<int>(pt.getProperty(IDs::controllerNumber, -1)) != controllerNumber)
                    continue;
                CcPointSnapshot s;
                s.ccId = static_cast<int>(pt.getProperty(IDs::ccID, 0));
                s.controllerNumber = controllerNumber;
                s.beat = pt.getProperty(IDs::beat, 0.0);
                s.value = static_cast<int>(pt.getProperty(IDs::value, 0));
                points.push_back(s);
            }
            // Sort by beat for display.
            std::sort(points.begin(), points.end(),
                      [](const CcPointSnapshot& a, const CcPointSnapshot& b) { return a.beat < b.beat; });
            return points;
        }
    }
    return {};
}

std::vector<ClipSnapshot::GainEnvelopePoint> ReadModelImpl::getClipGainEnvelope(int clipId) const
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

            std::vector<ClipSnapshot::GainEnvelopePoint> points;
            auto envelope = clipTree.getChildWithName(IDs::GAIN_ENVELOPE);
            if (!envelope.isValid())
                return points;

            double bpm = model_.getTree().getProperty(IDs::tempo, 120.0);
            points.reserve(envelope.getNumChildren());
            for (int i = 0; i < envelope.getNumChildren(); ++i) {
                auto pt = envelope.getChild(i);
                ClipSnapshot::GainEnvelopePoint p;
                double tSec = pt.getProperty(IDs::pointTime, 0.0);
                p.time = HDAW::secondsToBeats(tSec, bpm);
                p.gain = pt.getProperty(IDs::pointGain);
                points.push_back(p);
            }
            return points;
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
        ts.timeSigNumerator = transport.getProperty(IDs::timeSigNumerator, 4);
        ts.timeSigDenominator = transport.getProperty(IDs::timeSigDenominator, 4);
        // Tree stores loop region in seconds (engine consumers read seconds);
        // the frontend expects beats. Convert seconds → beats.
        double ls = transport.getProperty(IDs::loopStart, 0.0);
        double le = transport.getProperty(IDs::loopEnd, 8.0);
        ts.loopStart = HDAW::secondsToBeats(ls, ts.bpm);
        ts.loopEnd   = HDAW::secondsToBeats(le, ts.bpm);
        // Use the live TransportManager position (audio-thread atomic advanced
        // each processBlock) instead of the ValueTree position property, which
        // is only written on seek/stop and never updated during playback.
        if (engine_ != nullptr) {
            ts.currentTimeSeconds = engine_->getTransportManager().getCurrentPositionSeconds();
            ts.isRecording = engine_->getTransportManager().isRecordingNow();
            ts.punchEnabled = engine_->getTransportManager().isPunchEnabled();
        } else {
            ts.currentTimeSeconds = transport.getProperty(IDs::position, 0.0);
        }
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
        s.pluginFormat = slot.getProperty(IDs::pluginFormat, "").toString().toStdString();
        s.bypassed = slot.getProperty(IDs::bypassed, false);
        s.paramCount = slot.getNumChildren();
        result.push_back(s);
    }
    return result;
}

std::vector<MidiFxSlotSnapshot> ReadModelImpl::getMidiFxSlots(int trackIndex) const
{
    std::vector<MidiFxSlotSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;

    auto chain = trackList.getChild(trackIndex).getChildWithName(IDs::MIDI_FX_CHAIN);
    if (!chain.isValid())
        return result;

    for (int i = 0; i < chain.getNumChildren(); ++i)
    {
        auto slot = chain.getChild(i);
        MidiFxSlotSnapshot s;
        s.slotIndex = i;
        s.fxType = slot.getProperty(IDs::fxType, "").toString().toStdString();
        s.bypassed = slot.getProperty(IDs::bypassed, false);
        static const std::set<juce::String> reserved = { "fxType", "bypassed" };
        for (int p = 0; p < slot.getNumProperties(); ++p)
        {
            auto name = slot.getPropertyName(p).toString();
            if (reserved.count(name) == 0)
            {
                auto val = slot.getProperty(slot.getPropertyName(p));
                if (val.isDouble() || val.isInt() || val.isInt64())
                    s.params[name.toStdString()] = static_cast<double>(val);
                else if (val.isString())
                    s.params[name.toStdString()] = val.toString().toStdString();
            }
        }
        result.push_back(s);
    }
    return result;
}

std::vector<InternalFxParamSnapshot> ReadModelImpl::getInternalFxParams(int trackIndex,
    int slotIndex) const
{
    std::vector<InternalFxParamSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return result;

    auto slotTree = fxChain.getChild(slotIndex);
    juce::String fxType = slotTree.getProperty(IDs::fxType).toString();
    if (fxType == "plugin" || fxType.isEmpty())
        return result;

    auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxType.toStdString());
    for (const auto& def : defs)
    {
        InternalFxParamSnapshot snap;
        snap.paramIndex = def.index;
        snap.name = def.name.toStdString();
        snap.defaultValue = def.defaultValue;
        snap.minValue = def.minValue;
        snap.maxValue = def.maxValue;

        juce::String propName = "param_" + juce::String(def.index);
        if (slotTree.hasProperty(juce::Identifier(propName)))
            snap.value = static_cast<float>(slotTree.getProperty(juce::Identifier(propName)));
        else
            snap.value = def.defaultValue;

        result.push_back(snap);
    }
    return result;
}

SamplerStateSnapshot ReadModelImpl::getSamplerState(int trackIndex, int slotIndex) const
{
    SamplerStateSnapshot snap;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return snap;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return snap;

    auto slotTree = fxChain.getChild(slotIndex);
    juce::String fxType = slotTree.getProperty(IDs::fxType).toString();
    if (fxType != "sampler")
        return snap;

    snap.sampleFile = slotTree.getProperty("sampleFile", "").toString().toStdString();
    snap.mode = slotTree.getProperty("mode", "classic").toString().toStdString();
    snap.rootNote = static_cast<int>(slotTree.getProperty("rootNote", 60));
    snap.transpose = static_cast<int>(slotTree.getProperty("transpose", 0));
    snap.mono = static_cast<bool>(slotTree.getProperty("mono", false));
    snap.playReverse = static_cast<bool>(slotTree.getProperty("playReverse", false));

    snap.attack = static_cast<float>(slotTree.getProperty("param_0", 0.005));
    snap.decay = static_cast<float>(slotTree.getProperty("param_1", 0.1));
    snap.sustain = static_cast<float>(slotTree.getProperty("param_2", 0.9));
    snap.release = static_cast<float>(slotTree.getProperty("param_3", 0.1));
    snap.hold = static_cast<float>(slotTree.getProperty("param_6", 0.0));
    snap.glide = static_cast<float>(slotTree.getProperty("param_7", 0.0));
    snap.sampleStart = static_cast<float>(slotTree.getProperty("param_5", 0.0));
    snap.sampleEnd = static_cast<float>(slotTree.getProperty("param_9", 1.0));

    snap.sliceMode = slotTree.getProperty("sliceMode", "transient").toString().toStdString();
    snap.sliceGrid = static_cast<double>(slotTree.getProperty("sliceGrid", 0.25));
    snap.sliceSensitivity = static_cast<double>(slotTree.getProperty("sliceSensitivity", 0.5));
    juce::String sliceStr = slotTree.getProperty("slicePoints", "").toString();
    for (auto& tok : juce::StringArray::fromTokens(sliceStr, ",", ""))
        snap.slicePoints.push_back(static_cast<float>(tok.trim().getDoubleValue()));

    snap.keyRangeLow = static_cast<int>(slotTree.getProperty("keyRangeLow", -1));
    snap.keyRangeHigh = static_cast<int>(slotTree.getProperty("keyRangeHigh", -1));

    if (engine_)
    {
        auto* proc = engine_->getMainProcessor();
        if (proc)
        {
            auto* track = proc->getTrack(trackIndex);
            if (track)
            {
                auto& chain = track->getFXChain();
                if (slotIndex < static_cast<int>(chain.size()) && chain[slotIndex])
                {
                    auto* sampler = chain[slotIndex]->samplerEngineForTest();
                    snap.hasSound = (sampler != nullptr && sampler->currentSound() != nullptr);
                    if (sampler)
                        snap.activeVoices = sampler->activeVoiceCount();
                }
            }
        }
    }

    return snap;
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
        l.mode = lane.getProperty(IDs::automationMode, "read").toString().toStdString();
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

        double bpm = model_.getTree().getProperty(IDs::tempo, 120.0);
        for (int p = 0; p < pointList.getNumChildren(); ++p)
        {
            auto pt = pointList.getChild(p);
            AutomationPointSnapshot aps;
            double tSec = pt.getProperty(IDs::startTime, 0.0);
            aps.time = HDAW::secondsToBeats(tSec, bpm);
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

std::vector<ArrangerRegionSnapshot> ReadModelImpl::getArrangerRegions() const
{
    std::vector<ArrangerRegionSnapshot> result;
    auto arrangerList = model_.getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid())
        return result;

    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto region = arrangerList.getChild(i);
        ArrangerRegionSnapshot rs;
        rs.regionID = region.getProperty(IDs::regionID, "").toString().toStdString();
        rs.name = region.getProperty(IDs::regionName, "").toString().toStdString();
        rs.startTime = static_cast<double>(region.getProperty(IDs::startTime, 0.0));
        rs.duration = static_cast<double>(region.getProperty(IDs::duration, 0.0));
        rs.color = static_cast<int>(region.getProperty(IDs::color, 0));
        result.push_back(rs);
    }
    return result;
}

std::vector<ArrangerChainSnapshot> ReadModelImpl::getArrangerChains() const
{
    std::vector<ArrangerChainSnapshot> result;
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid())
        return result;

    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto chain = chainList.getChild(i);
        ArrangerChainSnapshot cs;
        cs.chainID = chain.getProperty(IDs::chainID, "").toString().toStdString();
        cs.name = chain.getProperty(IDs::chainName, "").toString().toStdString();
        cs.isActive = static_cast<bool>(chain.getProperty(IDs::isActive, false));

        for (int e = 0; e < chain.getNumChildren(); ++e)
        {
            auto entry = chain.getChild(e);
            ChainEntrySnapshot es;
            es.regionID = entry.getProperty(IDs::regionID, "").toString().toStdString();
            es.repeatCount = static_cast<int>(entry.getProperty(IDs::repeatCount, 1));
            cs.entries.push_back(es);
        }
        result.push_back(cs);
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
    // pid ranges (see docs/adr-automation-model.md): 1/2/3 = volume/pan/mute,
    // 100..999 = fxChain slot*100+param, 1000..1999 = midiFxChain slot*100+param.
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

    // Walk the live MIDI FX chain. midiFx entries carry the FULL compound pid
    // (1000 + slotIndex*100 + paramIndex) in paramIndex — callers pass it
    // through as-is (unlike audio slots, whose paramIndex is the bare index).
    auto& midiFxChain = track->getMidiFxChain();
    for (int si = 0; si < static_cast<int>(midiFxChain.size()); ++si)
    {
        auto& slot = midiFxChain[si];
        if (!slot || slot->isBypassed())
            continue;

        const auto& params = slot->getAutomatableParams();
        for (const auto& p : params)
        {
            AutomatableParamSnapshot aps;
            aps.slotIndex = si;
            aps.paramIndex = 1000 + si * 100 + p.index;
            aps.name = slot->getType().toStdString() + "." + p.name.toStdString();
            aps.automatable = true;
            result.push_back(aps);
        }
    }

    return result;
}

std::vector<LfoSnapshot> ReadModelImpl::getModulationLfos(int trackIndex) const
{
    std::vector<LfoSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return result;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid()) return result;

    for (int i = 0; i < modList.getNumChildren(); ++i)
    {
        auto lfo = modList.getChild(i);
        LfoSnapshot snap;
        snap.index = i;
        snap.name = lfo.getProperty(IDs::name, "").toString().toStdString();
        snap.waveform = static_cast<int>(lfo.getProperty(IDs::waveform, 0));
        snap.rate = lfo.getProperty(IDs::rate, 1.0);
        snap.rateSync = lfo.getProperty(IDs::rateSync, true);
        snap.depth = lfo.getProperty(IDs::depth, 0.3);
        snap.bipolar = lfo.getProperty(IDs::bipolar, false);
        snap.phaseOffset = lfo.getProperty(IDs::phaseOffset, 0.0);
        snap.targetParamID = static_cast<int>(lfo.getProperty(IDs::targetParamID, 1));
        snap.enabled = lfo.getProperty(IDs::enabled, true);
        result.push_back(snap);
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
    ms.rmsLeftLevel = track->getMeter().getRmsLeft();
    ms.rmsRightLevel = track->getMeter().getRmsRight();
    ms.lufsMomentary = track->getMeter().getLufsMomentary();
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
    ms.rmsLeftLevel = proc->getMasterMeter().getRmsLeft();
    ms.rmsRightLevel = proc->getMasterMeter().getRmsRight();
    ms.lufsMomentary = proc->getMasterMeter().getLufsMomentary();
    return ms;
}

FmAnalysisSnapshot ReadModelImpl::getFmAnalysis(int trackIndex) const
{
    if (engine_ == nullptr) return {};
    auto* proc = engine_->getMainProcessor();
    if (proc == nullptr) return {};
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr) return {};

    FmAnalysisSnapshot snap;
    auto& fxChain = track->getFXChain();
    for (int si = 0; si < static_cast<int>(fxChain.size()); ++si)
    {
        auto& slot = fxChain[si];
        if (!slot || slot->isBypassed())
            continue;
        if (slot->getType() == "fm_synth")
        {
            auto* fm = slot->fmSynthEngine();
            if (fm != nullptr)
            {
                for (int op = 0; op < 6; ++op)
                    snap.opEgLevel[op] = fm->getOpEgLevel(op);
                snap.activeVoices = fm->getAnalysisVoiceCount();
                snap.algorithm = fm->getAnalysisAlgorithm();
            }
            break;
        }
    }
    return snap;
}

bool ReadModelImpl::isDirty() const
{
    return model_.isDirty();
}

std::vector<SendSnapshot> ReadModelImpl::getTrackSends(int trackIndex) const
{
    std::vector<SendSnapshot> result;
    auto trackList = model_.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;
    auto sendList = trackList.getChild(trackIndex).getChildWithName(IDs::SEND_LIST);
    if (!sendList.isValid())
        return result;
    for (int s = 0; s < sendList.getNumChildren(); ++s)
    {
        auto sendTree = sendList.getChild(s);
        SendSnapshot snap;
        snap.sendIndex = s;
        snap.level = sendTree.getProperty(IDs::sendLevel, 0.0f);
        snap.isPreFader = sendTree.getProperty(IDs::sendMode, "post").toString() == "pre";
        snap.bypassed = sendTree.getProperty(IDs::bypassed, false);
        result.push_back(snap);
    }
    return result;
}
