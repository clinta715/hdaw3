#include "engine/ReadModelImpl.h"
#include "model/ProjectModel.h"

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
        return {};

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
        ts.currentSample = transport.getProperty(IDs::position, 0.0);
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
