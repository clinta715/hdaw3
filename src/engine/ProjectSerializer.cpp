#include "ProjectSerializer.h"
#include "common/Version.h"
#include "../common/DebugLog.h"

namespace HDAW {

namespace {

// Load-time migration hook. Reads formatVersion (default 0 for legacy files
// that predate metadata stamping). This is READ-ONLY for provenance: it must
// NOT stamp the current app version or formatVersion onto the loaded tree —
// that would destroy the "created with" history. Provenance is re-stamped on
// the next save() only when the property is genuinely absent.
//
// Future schema bumps branch on `fmt` here and mutate the tree as needed; the
// current contract is that fmt=0 (legacy) loads cleanly without modification.
void migrateProjectTree(juce::ValueTree& root)
{
    const int fmt = static_cast<int>(root.getProperty(IDs::formatVersion, 0));
    if (fmt < 1)
    {
        // Legacy file (or no metadata at all). We deliberately do NOT rewrite
        // provenance here — load is read-only. The next save() backfills the
        // missing metadata via the createIfAbsent path. Log so the hook is
        // visibly wired and a future migration has a place to land.
        HDAW_LOG("migrate", std::string("legacy formatVersion=") + std::to_string(fmt)
                 + " loaded without rewrite; provenance will be backfilled on next save");
    }
}

} // namespace

bool ProjectSerializer::save(ProjectModel& model, const juce::File& file)
{
    auto& tree = model.getTree();

    // Backfill provenance for legacy in-memory trees that never had it stamped
    // (e.g. loaded from a metadata-less file then re-saved). Use a nullptr
    // undo manager — save is not an undoable op.
    if (!tree.hasProperty(IDs::createdWithApp))
        tree.setProperty(IDs::createdWithApp, juce::String(HDAW_VERSION), nullptr);
    if (!tree.hasProperty(IDs::formatVersion))
        tree.setProperty(IDs::formatVersion, 1, nullptr);
    if (!tree.hasProperty(IDs::createdAt))
        tree.setProperty(IDs::createdAt, juce::Time::getCurrentTime().toISO8601(true), nullptr);

    // Always refresh save-time metadata.
    tree.setProperty(IDs::savedWithApp, juce::String(HDAW_VERSION), nullptr);
    tree.setProperty(IDs::lastSavedAt, juce::Time::getCurrentTime().toISO8601(true), nullptr);

    auto xml = tree.toXmlString();
    if (xml.isEmpty())
        return false;

    if (!file.replaceWithText(xml))
        return false;

    model.markAsSaved();
    return true;
}

bool ProjectSerializer::load(ProjectModel& model, const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    auto xml = file.loadFileAsString();
    if (xml.isEmpty())
        return false;

    auto newTree = juce::ValueTree::fromXml(xml);
    if (!newTree.isValid())
        return false;

    if (!newTree.hasType(IDs::PROJECT))
        return false;

    if (!newTree.getChildWithName(IDs::TRACK_LIST).isValid())
        return false;

    auto& undoManager = model.getUndoManager();

    model.getTree().removeAllProperties(&undoManager);
    model.getTree().removeAllChildren(&undoManager);

    model.getTree().copyPropertiesFrom(newTree, &undoManager);
    for (int i = 0; i < newTree.getNumChildren(); ++i)
        model.getTree().addChild(newTree.getChild(i).createCopy(), -1, &undoManager);

    // Migration hook: schema upgrades land here. READ-ONLY for provenance
    // (createdWithApp / createdAt / savedWithApp stay as-read from the file;
    // they are re-stamped on next save). Runs before clearUndoHistory so any
    // tree mutation it performs is not retained as an undoable action.
    migrateProjectTree(model.getTree());

    model.getUndoManager().clearUndoHistory();
    model.scanAndSyncClipIDs();
    model.scanAndSyncNoteIDs();

    // Never auto-play on load — clear any stale isPlaying/position that
    // may have been serialized from a project that was playing on save.
    auto transportTree = model.getTransportTree();
    if (transportTree.isValid())
    {
        transportTree.setProperty(IDs::isPlaying, false, nullptr);
        transportTree.setProperty(IDs::position, 0.0, nullptr);
    }

    model.markAsSaved();
    return true;
}

void ProjectSerializer::createNew(ProjectModel& model)
{
    model.createDefaultProject();
    model.markAsSaved();
}

} // namespace HDAW
