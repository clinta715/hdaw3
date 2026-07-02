#include "ClipClipboard.h"
#include "../model/ProjectModel.h"

namespace HDAW
{
namespace
{
std::vector<ClipboardEntry> g_clipboard;
ClipboardMeta g_meta;
}

ClipboardEntry ClipClipboard::deepCopy(const juce::ValueTree& sourceClip)
{
    ClipboardEntry entry;
    if (sourceClip.isValid())
    {
        // ValueTree copy constructor does a deep copy including all children
        // (MIDI_NOTE_LIST, TAKE_LIST, etc.). Source file paths are stored
        // as strings so they are copied by value — no file duplication.
        entry.clipTree = sourceClip.createCopy();
        entry.sourceStartTime = sourceClip.getProperty(IDs::startTime, 0.0);
    }
    return entry;
}

void ClipClipboard::copyClips(const std::vector<ClipboardEntry>& entries)
{
    g_clipboard = entries;
    // Compute the minimum startTime so paste can offset the whole group.
    g_meta.minStartTime = 0.0;
    if (!entries.empty())
    {
        double minStart = entries[0].sourceStartTime;
        for (const auto& e : entries)
            minStart = std::min(minStart, e.sourceStartTime);
        g_meta.minStartTime = minStart;
    }
}

const std::vector<ClipboardEntry>& ClipClipboard::getClips()
{
    return g_clipboard;
}

const ClipboardMeta& ClipClipboard::getMeta()
{
    return g_meta;
}

bool ClipClipboard::hasContent()
{
    return !g_clipboard.empty();
}

void ClipClipboard::clear()
{
    g_clipboard.clear();
}
}
