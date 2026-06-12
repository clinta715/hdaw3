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
        ccList = clip.getChildWithName(IDs::CC_LIST);
        if (!ccList.isValid() && clip.isValid())
        {
            ccList = juce::ValueTree(IDs::CC_LIST);
            clip.addChild(ccList, -1, nullptr);
        }
    }

    juce::ValueTree getClipTree() const { return clipTree; }
    juce::ValueTree getNoteList() const { return noteList; }
    juce::ValueTree getCcList() const { return ccList; }

    bool isValid() const { return clipTree.isValid() && noteList.isValid(); }
    void clear() { clipTree = {}; noteList = {}; ccList = {}; }

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

    // --- Clipboard API ---

    void copySelectedNotes()
    {
        clipboardNotes.clear();
        for (auto& n : selectedNotes)
        {
            juce::ValueTree copy(IDs::MIDI_NOTE);
            copy.setProperty(IDs::noteNumber, n.getProperty(IDs::noteNumber), nullptr);
            copy.setProperty(IDs::velocity, n.getProperty(IDs::velocity), nullptr);
            copy.setProperty(IDs::startBeat, n.getProperty(IDs::startBeat), nullptr);
            copy.setProperty(IDs::durationBeats, n.getProperty(IDs::durationBeats), nullptr);
            clipboardNotes.add(copy);
        }
    }

    void pasteNotes(double targetBeat)
    {
        if (clipboardNotes.isEmpty()) return;
        if (!noteList.isValid()) return;

        if (undoManager) undoManager->beginNewTransaction();

        double minBeat = clipboardNotes[0].getProperty(IDs::startBeat);
        for (auto& n : clipboardNotes)
        {
            double sb = n.getProperty(IDs::startBeat);
            if (sb < minBeat) minBeat = sb;
        }

        for (auto& n : clipboardNotes)
        {
            juce::ValueTree note(IDs::MIDI_NOTE);
            double origBeat = n.getProperty(IDs::startBeat);
            note.setProperty(IDs::noteNumber, n.getProperty(IDs::noteNumber), undoManager);
            note.setProperty(IDs::velocity, n.getProperty(IDs::velocity), undoManager);
            note.setProperty(IDs::startBeat, targetBeat + (origBeat - minBeat), undoManager);
            note.setProperty(IDs::durationBeats, n.getProperty(IDs::durationBeats), undoManager);
            noteList.addChild(note, -1, undoManager);
        }
    }

    bool hasClipboard() const { return !clipboardNotes.isEmpty(); }
    void clearClipboard() { clipboardNotes.clear(); }

    // --- CC API ---

    int getCcPointCount(int controllerNumber) const
    {
        if (!ccList.isValid()) return 0;
        int count = 0;
        for (int i = 0; i < ccList.getNumChildren(); ++i)
            if (static_cast<int>(ccList.getChild(i).getProperty(IDs::controllerNumber)) == controllerNumber)
                ++count;
        return count;
    }

    juce::ValueTree getCcPoint(int controllerNumber, int index) const
    {
        if (!ccList.isValid()) return {};
        int seen = 0;
        for (int i = 0; i < ccList.getNumChildren(); ++i)
        {
            auto child = ccList.getChild(i);
            if (static_cast<int>(child.getProperty(IDs::controllerNumber)) == controllerNumber)
            {
                if (seen == index)
                    return child;
                ++seen;
            }
        }
        return {};
    }

    juce::ValueTree addCcPoint(int controllerNumber, double beat, int value)
    {
        if (!ccList.isValid()) return {};
        if (undoManager) undoManager->beginNewTransaction();
        juce::ValueTree pt(IDs::CC_POINT);
        pt.setProperty(IDs::controllerNumber, controllerNumber, undoManager);
        pt.setProperty(IDs::beat, beat, undoManager);
        pt.setProperty(IDs::value, value, undoManager);
        ccList.addChild(pt, -1, undoManager);
        return pt;
    }

private:
    juce::ValueTree clipTree;
    juce::ValueTree noteList;
    juce::ValueTree ccList;
    juce::Array<juce::ValueTree> selectedNotes;
    juce::Array<juce::ValueTree> clipboardNotes;
    juce::UndoManager* undoManager = nullptr;
};
