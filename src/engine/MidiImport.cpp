#include "MidiImport.h"
#include "../model/ProjectModel.h"
#include <QMessageBox>
#include <QInputDialog>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

bool HDAW::importMidiFile(AudioEngine& engine, QWidget* parent, const QString& path)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0) return false;

    juce::File midiFile(path.toUtf8().constData());
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
    {
        QMessageBox::warning(parent, "Error", "Could not open MIDI file.");
        return false;
    }

    juce::MidiFile midiData;
    if (!midiData.readFrom(stream))
    {
        QMessageBox::warning(parent, "Error", "Failed to read MIDI file.");
        return false;
    }

    int midiTimeFormat = static_cast<int>(midiData.getTimeFormat());
    if (midiTimeFormat <= 0)
    {
        QMessageBox::warning(parent, "Error", "SMPTE timecode MIDI files are not supported.");
        return false;
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

    QStringList trackNames;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto name = QString::fromUtf8(
            trackList.getChild(i).getProperty(IDs::name).toString().toRawUTF8());
        trackNames << QString("Track %1: %2").arg(i + 1).arg(name);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(parent, "Select Track",
        "Import to which track?", trackNames, 0, false, &ok);
    if (!ok || selected.isEmpty()) return false;

    int trackIndex = trackNames.indexOf(selected);
    if (trackIndex < 0) return false;

    for (int mt = 0; mt < midiData.getNumTracks(); ++mt)
    {
        auto* midiTrack = midiData.getTrack(mt);
        if (midiTrack == nullptr || midiTrack->getNumEvents() == 0)
            continue;

        double clipDuration = 4.0;
        auto* lastEventHolder = midiTrack->getEventPointer(midiTrack->getNumEvents() - 1);
        if (lastEventHolder != nullptr)
            clipDuration = lastEventHolder->message.getTimeStamp() * secondsPerTick + 1.0;

        auto trackTree = trackList.getChild(trackIndex);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
        {
            clipList = juce::ValueTree(IDs::CLIP_LIST);
            trackTree.addChild(clipList, -1, &engine.getProjectModel().getUndoManager());
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
            clipList.addChild(clip, -1, &engine.getProjectModel().getUndoManager());
        }
    }

    engine.getMainProcessor()->rebuildRoutingGraph();
    return true;
}
