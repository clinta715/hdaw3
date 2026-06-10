#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "../model/ProjectModel.h"

class PianoRollModel
{
public:
    PianoRollModel() = default;
    ~PianoRollModel() = default;

    void setClipTree(juce::ValueTree clip)
    {
        clipTree = clip;
        noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!noteList.isValid() && clip.isValid())
        {
            // Defensive: ensure the clip has a note container so addNote() doesn't silently no-op.
            // No undo manager: this implicit creation should not pollute the undo history.
            noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            clip.addChild(noteList, -1, nullptr);
        }
    }

    juce::ValueTree getClipTree() const { return clipTree; }
    juce::ValueTree getNoteList() const { return noteList; }

    bool isValid() const { return clipTree.isValid() && noteList.isValid(); }
    void clear() { clipTree = {}; noteList = {}; }

    int getNumNotes() const { return noteList.isValid() ? noteList.getNumChildren() : 0; }
    juce::ValueTree getNote(int index) const { return noteList.getChild(index); }

    void setUndoManager(juce::UndoManager* um) { undoManager = um; }
    juce::UndoManager* getUndoManager() const { return undoManager; }

    juce::ValueTree addNote(int noteNumber, float velocity, double startBeat, double durationBeats)
    {
        if (!noteList.isValid()) return {};

        if (undoManager) undoManager->beginNewTransaction();
        juce::ValueTree note(IDs::MIDI_NOTE);
        note.setProperty(IDs::noteNumber, noteNumber, undoManager);
        note.setProperty(IDs::velocity, velocity, undoManager);
        note.setProperty(IDs::startBeat, startBeat, undoManager);
        note.setProperty(IDs::durationBeats, durationBeats, undoManager);
        noteList.addChild(note, -1, undoManager);
        return note;
    }

    void removeNote(juce::ValueTree note)
    {
        if (noteList.isValid())
        {
            if (undoManager) undoManager->beginNewTransaction();
            noteList.removeChild(note, undoManager);
        }
    }

    const juce::Array<juce::ValueTree>& getSelectedNotes() const { return selectedNotes; }
    void selectNote(juce::ValueTree note, bool addToSelection = false)
    {
        if (!addToSelection)
            selectedNotes.clear();
        if (!selectedNotes.contains(note))
            selectedNotes.add(note);
    }

    void deselectAll() { selectedNotes.clear(); }

    void removeSelectedNotes()
    {
        for (auto& n : selectedNotes)
            removeNote(n);
        selectedNotes.clear();
    }

private:
    juce::ValueTree clipTree;
    juce::ValueTree noteList;
    juce::Array<juce::ValueTree> selectedNotes;
    juce::UndoManager* undoManager = nullptr;
};
