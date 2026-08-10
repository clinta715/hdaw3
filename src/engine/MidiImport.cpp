#include "MidiImport.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

std::vector<int> HDAW::importMidiFile(AudioEngine& engine, const QString& path, int trackIdx)
{
    std::vector<int> importedClipIds;
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();

    juce::File midiFile(path.toUtf8().constData());
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
    {
        HDAW_LOG("MidiImport", "could not open MIDI file: " + path);
        return {};
    }

    juce::MidiFile midiData;
    if (!midiData.readFrom(stream))
    {
        HDAW_LOG("MidiImport", "failed to read MIDI file: " + path);
        return {};
    }

    int midiTimeFormat = static_cast<int>(midiData.getTimeFormat());
    if (midiTimeFormat <= 0)
    {
        HDAW_LOG("MidiImport", "SMPTE timecode MIDI files are not supported: " + path);
        return {};
    }
    int midiTicksPerQuarterNote = midiTimeFormat;
    double bpm = 120.0;

    if (midiData.getNumTracks() > 0)
    {
        auto* tempoTrack = midiData.getTrack(0);
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e)
        {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTempoMetaEvent())
            {
                double secPerQuarter = ev->message.getTempoSecondsPerQuarterNote();
                if (secPerQuarter > 0.0)
                    bpm = 60.0 / secPerQuarter;
                break;
            }
        }
    }

    double secondsPerTick = (60.0 / bpm) / static_cast<double>(midiTicksPerQuarterNote);

    for (int mt = 0; mt < midiData.getNumTracks(); ++mt)
    {
        auto* midiTrack = midiData.getTrack(mt);
        if (midiTrack == nullptr || midiTrack->getNumEvents() == 0)
            continue;

        double clipDuration = 4.0;
        auto* lastEventHolder = midiTrack->getEventPointer(midiTrack->getNumEvents() - 1);
        if (lastEventHolder != nullptr)
            clipDuration = lastEventHolder->message.getTimeStamp() * secondsPerTick + 1.0;

        // Resolve target track
        int targetTrackIdx = trackIdx;
        if (targetTrackIdx < 0)
        {
            // Create a new track for this MIDI track
            juce::String trackName = "MIDI Track " + juce::String(mt + 1);
            targetTrackIdx = engine.getProjectCommands().addTrack(
                trackName.toRawUTF8(), -1, -1, 1 /* MIDI */);
            if (targetTrackIdx < 0)
            {
                HDAW_LOG("MidiImport", "failed to create track for MIDI track " + juce::String(mt + 1));
                continue;
            }
        }

        auto trackTree = trackList.getChild(targetTrackIdx);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
        {
            clipList = juce::ValueTree(IDs::CLIP_LIST);
            trackTree.addChild(clipList, -1, &model.getUndoManager());
        }

        double clipStartTime = 0.0;
        for (int i = 0; i < clipList.getNumChildren(); ++i)
        {
            auto c = clipList.getChild(i);
            double end = static_cast<double>(c.getProperty(IDs::startTime))
                       + static_cast<double>(c.getProperty(IDs::duration));
            clipStartTime = (std::max)(clipStartTime, end);
        }

        auto clip = ProjectModel::createMidiClipEmpty(
            ("MIDI Track " + juce::String(mt + 1)).toRawUTF8(),
            clipStartTime, clipDuration);
        auto midiNotes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);

        for (int e = 0; e < midiTrack->getNumEvents(); ++e)
        {
            auto* eventHolder = midiTrack->getEventPointer(e);
            if (eventHolder == nullptr) continue;

            auto& msg = eventHolder->message;
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                double tickTime = msg.getTimeStamp();
                double beatTime = tickTime / static_cast<double>(midiTicksPerQuarterNote);

                double noteDurBeats = 0.25;
                int noteNum = msg.getNoteNumber();
                for (int e2 = e + 1; e2 < midiTrack->getNumEvents(); ++e2)
                {
                    auto* ev2 = midiTrack->getEventPointer(e2);
                    if (ev2 != nullptr && ev2->message.isNoteOff() &&
                        ev2->message.getNoteNumber() == noteNum)
                    {
                        double offTick = ev2->message.getTimeStamp();
                        noteDurBeats = (offTick - tickTime) / static_cast<double>(midiTicksPerQuarterNote);
                        break;
                    }
                }

                midiNotes.addChild(ProjectModel::createMidiNote(
                    noteNum, static_cast<float>(msg.getVelocity()) / 127.0f,
                    beatTime, noteDurBeats), -1, nullptr);
            }
        }

        if (midiNotes.getNumChildren() > 0)
        {
            clipList.addChild(clip, -1, &model.getUndoManager());
            // Read back the clip ID assigned by the model
            int clipId = static_cast<int>(clip.getProperty(IDs::clipID, -1));
            if (clipId >= 0)
                importedClipIds.push_back(clipId);
        }
    }

    engine.getMainProcessor()->rebuildRoutingGraph();
    return importedClipIds;
}
