#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — Ghost clips ──────────────────────────────

int AudioEngineCommands::createGhostClip(int sourceClipId, double newStart, int newTrackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(sourceClipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    // If the source is itself a ghost, walk the chain to find the root
    int rootId = sourceClipId;
    while (true)
    {
        auto rootClip = findClipById(rootId, trackIdx);
        if (!rootClip.isValid()) break;
        int parentId = static_cast<int>(rootClip.getProperty(IDs::ghostSourceId, -1));
        if (parentId < 0) break;
        rootId = parentId;
    }

    // Deep-copy the clip
    auto newClip = clip.createCopy();
    int newId = ProjectModel::allocateClipID();
    newClip.setProperty(IDs::clipID, newId, nullptr);
    newClip.setProperty(IDs::ghostSourceId, rootId, nullptr);
    newClip.setProperty(IDs::isGhost, 1, nullptr);
    newClip.setProperty(IDs::startTime, newStart, nullptr);

    // Remove MIDI_NOTE_LIST so it gets a fresh one via the listener
    auto noteList = newClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (noteList.isValid())
        newClip.removeChild(noteList, nullptr);

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (newTrackIndex < 0 || newTrackIndex >= trackList.getNumChildren())
        return -1;
    auto clipList = trackList.getChild(newTrackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return -1;
    clipList.addChild(newClip, -1, &um);

    // Copy MIDI notes from root to ghost
    auto rootClip = findClipById(rootId, trackIdx);
    if (rootClip.isValid())
    {
        auto rootNoteList = rootClip.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (rootNoteList.isValid())
        {
            auto ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            newClip.addChild(ghostNoteList, -1, &um);
            for (int i = 0; i < rootNoteList.getNumChildren(); ++i)
            {
                auto noteCopy = rootNoteList.getChild(i).createCopy();
                noteCopy.setProperty(IDs::noteID, ProjectModel::allocateClipID(), &um);
                ghostNoteList.addChild(noteCopy, -1, &um);
            }
        }
    }

    return newId;
}

std::vector<int> AudioEngineCommands::paintClips(const std::vector<int>& sourceClipIds, double originBeat, double spacing, int targetTrackIndex, int count)
{
    std::vector<int> result;
    auto& um = engine_.getProjectModel().getUndoManager();
    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (targetTrackIndex < 0 || targetTrackIndex >= trackList.getNumChildren())
        return result;
    auto clipList = trackList.getChild(targetTrackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return result;

    // Get the start time of the first source clip as the pattern origin
    double patternOrigin = 0.0;
    if (!sourceClipIds.empty())
    {
        int tmpIdx = -1;
        auto firstClip = findClipById(sourceClipIds[0], tmpIdx);
        if (firstClip.isValid())
            patternOrigin = firstClip.getProperty(IDs::startTime, 0.0);
    }

    beginTransaction("Paint clips");

    for (int tile = 0; tile < count; ++tile)
    {
        for (size_t s = 0; s < sourceClipIds.size(); ++s)
        {
            int srcId = sourceClipIds[s];
            int tmpIdx = -1;
            auto srcClip = findClipById(srcId, tmpIdx);
            if (!srcClip.isValid()) continue;

            // Walk ghost chain to root
            int rootId = srcId;
            while (true)
            {
                auto rc = findClipById(rootId, tmpIdx);
                if (!rc.isValid()) break;
                int pid = static_cast<int>(rc.getProperty(IDs::ghostSourceId, -1));
                if (pid < 0) break;
                rootId = pid;
            }

            auto newClip = srcClip.createCopy();
            int newId = ProjectModel::allocateClipID();
            newClip.setProperty(IDs::clipID, newId, &um);
            newClip.setProperty(IDs::ghostSourceId, rootId, &um);
            newClip.setProperty(IDs::isGhost, 1, &um);

            // Remove existing MIDI_NOTE_LIST from the copy (we'll add fresh notes)
            auto existingNoteList = newClip.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (existingNoteList.isValid())
                newClip.removeChild(existingNoteList, &um);

            // Compute relative offset within the pattern
            double srcStart = srcClip.getProperty(IDs::startTime, 0.0);
            double relativeOffset = srcStart - patternOrigin;
            newClip.setProperty(IDs::startTime, originBeat + (tile + 1) * spacing + relativeOffset, &um);

            // Copy MIDI notes from root
            int rootIdx = -1;
            auto rootClip = findClipById(rootId, rootIdx);
            if (rootClip.isValid())
            {
                auto rootNoteList = rootClip.getChildWithName(IDs::MIDI_NOTE_LIST);
                if (rootNoteList.isValid())
                {
                    auto ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
                    newClip.addChild(ghostNoteList, -1, &um);
                    for (int n = 0; n < rootNoteList.getNumChildren(); ++n)
                    {
                        auto noteCopy = rootNoteList.getChild(n).createCopy();
                        noteCopy.setProperty(IDs::noteID, ProjectModel::allocateClipID(), &um);
                        ghostNoteList.addChild(noteCopy, -1, &um);
                    }
                }
            }

            clipList.addChild(newClip, -1, &um);
            result.push_back(newId);
        }
    }

    endTransaction();
    return result;
}
