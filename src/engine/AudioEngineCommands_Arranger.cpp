#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include <juce_core/juce_core.h>

static juce::String generateID()
{
    return juce::Uuid().toString();
}

static juce::ValueTree findRegionByID(juce::ValueTree arrangerList, const juce::String& id)
{
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto child = arrangerList.getChild(i);
        if (child.getProperty(IDs::regionID, "") == id)
            return child;
    }
    return {};
}

static juce::ValueTree findChainByID(juce::ValueTree chainList, const juce::String& id)
{
    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto child = chainList.getChild(i);
        if (child.getProperty(IDs::chainID, "") == id)
            return child;
    }
    return {};
}

// --- Arranger Regions ---

std::string AudioEngineCommands::addArrangerRegion(const std::string& name, double start, double dur, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto root = engine_.getProjectModel().getTree();
    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid())
    {
        arrangerList = { IDs::ARRANGER_LIST, {} };
        root.addChild(arrangerList, -1, &um);
    }

    juce::String id = generateID();
    juce::ValueTree region { IDs::ARRANGER_REGION };
    region.setProperty(IDs::regionID, id, &um);
    region.setProperty(IDs::regionName, juce::String(name), &um);
    region.setProperty(IDs::startTime, start, &um);
    region.setProperty(IDs::duration, dur, &um);
    region.setProperty(IDs::color, color, &um);
    arrangerList.addChild(region, -1, &um);
    return id.toStdString();
}

void AudioEngineCommands::removeArrangerRegion(const std::string& rid)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto root = engine_.getProjectModel().getTree();
    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;

    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        arrangerList.removeChild(region, &um);

    // Cascade: remove chain entries referencing this region
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    for (int c = 0; c < chainList.getNumChildren(); ++c)
    {
        auto chain = chainList.getChild(c);
        for (int e = chain.getNumChildren() - 1; e >= 0; --e)
        {
            auto entry = chain.getChild(e);
            if (entry.getProperty(IDs::regionID, "") == juce::String(rid))
                chain.removeChild(entry, &um);
        }
    }
}

void AudioEngineCommands::setArrangerRegionName(const std::string& rid, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto arrangerList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        region.setProperty(IDs::regionName, juce::String(name), &um);
}

void AudioEngineCommands::setArrangerRegionBounds(const std::string& rid, double start, double dur)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto arrangerList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
    {
        region.setProperty(IDs::startTime, start, &um);
        region.setProperty(IDs::duration, dur, &um);
    }
}

void AudioEngineCommands::setArrangerRegionColor(const std::string& rid, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto arrangerList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        region.setProperty(IDs::color, color, &um);
}

// --- Arranger Chains ---

std::string AudioEngineCommands::addArrangerChain(const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto root = engine_.getProjectModel().getTree();
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid())
    {
        chainList = { IDs::ARRANGER_CHAIN_LIST, {} };
        root.addChild(chainList, -1, &um);
    }

    juce::String id = generateID();
    bool isFirst = chainList.getNumChildren() == 0;

    juce::ValueTree chain { IDs::ARRANGER_CHAIN };
    chain.setProperty(IDs::chainID, id, &um);
    chain.setProperty(IDs::chainName, juce::String(name), &um);
    chain.setProperty(IDs::isActive, isFirst, &um); // auto-activate first chain
    chainList.addChild(chain, -1, &um);
    return id.toStdString();
}

void AudioEngineCommands::removeArrangerChain(const std::string& cid)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;

    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;

    bool wasActive = static_cast<bool>(chain.getProperty(IDs::isActive, false));
    chainList.removeChild(chain, &um);

    // If removed chain was active, activate another
    if (wasActive && chainList.getNumChildren() > 0)
        chainList.getChild(0).setProperty(IDs::isActive, true, &um);
}

void AudioEngineCommands::setArrangerChainName(const std::string& cid, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (chain.isValid())
        chain.setProperty(IDs::chainName, juce::String(name), &um);
}

void AudioEngineCommands::setArrangerChainActive(const std::string& cid)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;

    // Deactivate all
    for (int i = 0; i < chainList.getNumChildren(); ++i)
        chainList.getChild(i).setProperty(IDs::isActive, false, &um);

    // Activate target
    auto chain = findChainByID(chainList, juce::String(cid));
    if (chain.isValid())
        chain.setProperty(IDs::isActive, true, &um);
}

// --- Chain Entries ---

int AudioEngineCommands::addChainEntry(const std::string& cid, const std::string& rid, int repeatCount)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return -1;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return -1;

    juce::ValueTree entry { IDs::CHAIN_ENTRY };
    entry.setProperty(IDs::regionID, juce::String(rid), &um);
    entry.setProperty(IDs::repeatCount, juce::jmax(1, repeatCount), &um);
    int index = chain.getNumChildren();
    chain.addChild(entry, index, &um);
    return index;
}

void AudioEngineCommands::removeChainEntry(const std::string& cid, int entryIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (entryIndex >= 0 && entryIndex < chain.getNumChildren())
        chain.removeChild(entryIndex, &um);
}

void AudioEngineCommands::reorderChainEntry(const std::string& cid, int fromIndex, int toIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (fromIndex < 0 || fromIndex >= chain.getNumChildren()) return;
    if (toIndex < 0 || toIndex >= chain.getNumChildren()) return;

    chain.moveChild(fromIndex, toIndex, &um);
}

void AudioEngineCommands::setChainEntryRepeat(const std::string& cid, int entryIndex, int repeatCount)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto chainList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (entryIndex >= 0 && entryIndex < chain.getNumChildren())
        chain.getChild(entryIndex).setProperty(IDs::repeatCount, juce::jmax(1, repeatCount), &um);
}

// --- Flatten ---

void AudioEngineCommands::flattenArranger()
{
    // Stub — full implementation in Task 11
    jassertfalse;
}
