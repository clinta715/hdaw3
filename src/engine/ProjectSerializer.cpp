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
    model.markAsSaved();
    return true;
}

void ProjectSerializer::createNew(ProjectModel& model)
{
    model.createDefaultProject();
    model.markAsSaved();
}

} // namespace HDAW
