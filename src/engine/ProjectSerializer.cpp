#include "ProjectSerializer.h"

namespace HDAW {

bool ProjectSerializer::save(ProjectModel& model, const juce::File& file)
{
    auto xml = model.getTree().toXmlString();
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
