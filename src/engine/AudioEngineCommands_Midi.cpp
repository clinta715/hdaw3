#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — MIDI note operations ───────────────────────

int AudioEngineCommands::addNote(int clipId, int pitch, int velocity,
                                 double startBeat, double durationBeats)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return -1;

    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid())
    {
        noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
        clip.addChild(noteList, -1, nullptr);
    }

    auto note = engine_.getProjectModel().createMidiNote(
        pitch, static_cast<float>(velocity) / 127.0f, startBeat, durationBeats);
    int noteId = static_cast<int>(note.getProperty(IDs::noteID, 0));
    noteList.addChild(note, -1, &um);
    return noteId;
}

void AudioEngineCommands::removeNote(int noteId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.getParent().removeChild(note, &um);
}

void AudioEngineCommands::setNotePitch(int noteId, int pitch)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::noteNumber, pitch, &um);
}

void AudioEngineCommands::setNoteVelocity(int noteId, int velocity)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::velocity, static_cast<float>(velocity) / 127.0f, &um);
}

void AudioEngineCommands::setNoteStart(int noteId, double startBeat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::startBeat, startBeat, &um);
}

void AudioEngineCommands::setNoteDuration(int noteId, double durationBeats)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::durationBeats, durationBeats, &um);
}

void AudioEngineCommands::setNoteChance(int noteId, float chance)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::chance, static_cast<double>(chance), &um);
}

void AudioEngineCommands::setNoteRepeatCount(int noteId, int repeatCount)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::repeatCount, repeatCount, &um);
}

void AudioEngineCommands::setNoteRepeatRate(int noteId, float repeatRate)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::repeatRate, static_cast<double>(repeatRate), &um);
}

void AudioEngineCommands::setNoteRepeatCurve(int noteId, float repeatCurve)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::repeatCurve, static_cast<double>(repeatCurve), &um);
}

void AudioEngineCommands::setNoteOccurrence(int noteId, int occurrence)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::occurrence, occurrence, &um);
}

void AudioEngineCommands::setNoteRecurrence(int noteId, int recurrence)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::recurrence, recurrence, &um);
}

void AudioEngineCommands::setNoteGain(int noteId, float gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::noteGain, static_cast<double>(gain), &um);
}

void AudioEngineCommands::setNotePan(int noteId, float pan)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::notePan, static_cast<double>(pan), &um);
}

void AudioEngineCommands::setNotePitchOffset(int noteId, float pitchOffset)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::notePitch, static_cast<double>(pitchOffset), &um);
}

void AudioEngineCommands::setNoteTimbre(int noteId, float timbre)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::noteTimbre, static_cast<double>(timbre), &um);
}

void AudioEngineCommands::setNotePressure(int noteId, float pressure)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::notePressure, static_cast<double>(pressure), &um);
}

void AudioEngineCommands::setNotesExpression(int noteId, float gain, float pan, float pitchOffset, float timbre, float pressure)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (!note.isValid()) return;
    note.setProperty(IDs::noteGain, static_cast<double>(gain), &um);
    note.setProperty(IDs::notePan, static_cast<double>(pan), &um);
    note.setProperty(IDs::notePitch, static_cast<double>(pitchOffset), &um);
    note.setProperty(IDs::noteTimbre, static_cast<double>(timbre), &um);
    note.setProperty(IDs::notePressure, static_cast<double>(pressure), &um);
}

void AudioEngineCommands::setClipSeed(int clipId, uint64_t seed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;
    clip.setProperty(IDs::seed, static_cast<int64_t>(seed), &um);
}

void AudioEngineCommands::setNotesOperator(int clipId, int noteId, float chance, int repeatCount, float repeatRate, float repeatCurve, int occurrence, int recurrence)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int outClipId = -1;
    auto note = findNoteById(noteId, outClipId);
    if (!note.isValid()) return;
    note.setProperty(IDs::chance, static_cast<double>(chance), &um);
    note.setProperty(IDs::repeatCount, repeatCount, &um);
    note.setProperty(IDs::repeatRate, static_cast<double>(repeatRate), &um);
    note.setProperty(IDs::repeatCurve, static_cast<double>(repeatCurve), &um);
    note.setProperty(IDs::occurrence, occurrence, &um);
    note.setProperty(IDs::recurrence, recurrence, &um);
}

void AudioEngineCommands::clearNotes(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (noteList.isValid())
        noteList.removeAllChildren(&um);
}

void AudioEngineCommands::addCcPoint(int clipId, int controllerNumber, double beat, int value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto ccList = clip.getChildWithName(IDs::CC_LIST);
    if (!ccList.isValid())
    {
        ccList = juce::ValueTree(IDs::CC_LIST);
        clip.addChild(ccList, -1, nullptr);
    }

    juce::ValueTree pt(IDs::CC_POINT);
    pt.setProperty(IDs::ccID, engine_.getProjectModel().allocateCcID(), nullptr);
    pt.setProperty(IDs::controllerNumber, controllerNumber, &um);
    pt.setProperty(IDs::beat, beat, &um);
    pt.setProperty(IDs::value, value, &um);
    ccList.addChild(pt, -1, &um);
}

void AudioEngineCommands::setCcPoint(int ccId, double beat, int value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto pt = findCcPointById(ccId, clipId);
    if (!pt.isValid()) return;
    pt.setProperty(IDs::beat, beat, &um);
    pt.setProperty(IDs::value, value, &um);
}

void AudioEngineCommands::removeCcPoint(int ccId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto pt = findCcPointById(ccId, clipId);
    if (pt.isValid())
        pt.getParent().removeChild(pt, &um);
}

void AudioEngineCommands::setCcRecordArmed(bool armed)
{
    engine_.setMidiCcRecordArmed(armed);
    if (armed)
        engine_.setMidiCcCallback([this](int channel, int controller, int value) {
            engine_.recordMidiCc(channel, controller, value);
        });
    else
        engine_.setMidiCcCallback(nullptr);
}

void AudioEngineCommands::setMidiNoteRecordArmed(bool armed)
{
    engine_.setMidiNoteRecordArmed(armed);
}
