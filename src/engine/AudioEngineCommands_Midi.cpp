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

    auto note = ProjectModel::createMidiNote(
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
    pt.setProperty(IDs::controllerNumber, controllerNumber, &um);
    pt.setProperty(IDs::beat, beat, &um);
    pt.setProperty(IDs::value, value, &um);
    ccList.addChild(pt, -1, &um);
}
