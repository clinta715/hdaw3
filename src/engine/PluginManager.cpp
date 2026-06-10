#include "PluginManager.h"

namespace HDAW {

PluginManager::PluginManager()
{
    formatManager.addFormat(new juce::VST3PluginFormat());
    formatManager.addFormat(new CLAPPluginFormat());

    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto hdawDir = appData.getChildFile("HDAW");
    cacheFile = hdawDir.getChildFile("plugin_cache.xml");
    blacklistFile = hdawDir.getChildFile("plugin_blacklist.xml");

    loadBlacklist();
}

PluginManager::~PluginManager()
{
    saveCache();
}

void PluginManager::loadCache()
{
    if (cacheFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(cacheFile);
        if (xml != nullptr)
        {
            knownPluginList.recreateFromXml(*xml);
            knownPlugins.clear();
            const auto& types = knownPluginList.getTypes();
            for (const auto& desc : types)
                knownPlugins.push_back(desc);
        }
    }
}

void PluginManager::saveCache()
{
    cacheFile.getParentDirectory().createDirectory();
    if (auto xml = knownPluginList.createXml())
    {
        xml->writeTo(cacheFile, {});
    }
}

void PluginManager::scanAll()
{
    if (scanning.load()) return;
    scanning.store(true);

    loadCache();

    juce::StringArray defaultDirs;

#if JUCE_WINDOWS
    auto programsDir = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    defaultDirs.add(programsDir.getChildFile("Common Files\\VST3").getFullPathName());
    defaultDirs.add(programsDir.getChildFile("Common Files\\CLAP").getFullPathName());
#elif JUCE_MAC
    defaultDirs.add("/Library/Audio/Plug-Ins/VST3");
    defaultDirs.add("~/Library/Audio/Plug-Ins/VST3");
    defaultDirs.add("/Library/Audio/Plug-Ins/CLAP");
    defaultDirs.add("~/Library/Audio/Plug-Ins/CLAP");
#elif JUCE_LINUX
    defaultDirs.add("/usr/lib/vst3");
    defaultDirs.add("/usr/local/lib/vst3");
    defaultDirs.add("~/.vst3");
    defaultDirs.add("/usr/lib/clap");
    defaultDirs.add("/usr/local/lib/clap");
    defaultDirs.add("~/.clap");
#endif

    for (const auto& format : formatManager.getFormats())
    {
        for (const auto& dir : defaultDirs)
        {
            juce::File d(dir);
            if (d.isDirectory())
            {
                juce::PluginDirectoryScanner scanner(
                    knownPluginList, *format,
                    juce::FileSearchPath(d.getFullPathName()),
                    false, juce::File{}, false
                );

                juce::String name;
                while (scanner.scanNextFile(false, name))
                {
                    juce::Logger::writeToLog("PluginManager: found - " + name);
                }
            }
        }
    }

    onScanFinished();
}

void PluginManager::onScanFinished()
{
    knownPlugins.clear();
    const auto& types = knownPluginList.getTypes();
    for (const auto& desc : types)
        knownPlugins.push_back(desc);

    saveCache();
    scanning.store(false);

    if (scanCallback)
        scanCallback();
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createPluginInstance(
    const juce::PluginDescription& desc, juce::String& errorMessage,
    double sampleRate, int blockSize)
{
    if (isBlacklisted(desc.fileOrIdentifier))
    {
        errorMessage = "Plugin is blacklisted: " + desc.fileOrIdentifier;
        return nullptr;
    }
    return formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMessage);
}

bool PluginManager::isBlacklisted(const juce::String& pluginID) const
{
    for (const auto& id : blacklistedIDs)
        if (id == pluginID)
            return true;
    return false;
}

void PluginManager::blacklistPlugin(const juce::String& pluginID)
{
    if (!isBlacklisted(pluginID))
    {
        blacklistedIDs.push_back(pluginID);
        saveBlacklist();
    }
}

void PluginManager::unblacklistPlugin(const juce::String& pluginID)
{
    for (auto it = blacklistedIDs.begin(); it != blacklistedIDs.end(); ++it)
    {
        if (*it == pluginID)
        {
            blacklistedIDs.erase(it);
            saveBlacklist();
            return;
        }
    }
}

void PluginManager::loadBlacklist()
{
    blacklistedIDs.clear();
    if (!blacklistFile.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(blacklistFile);
    if (xml == nullptr)
        return;

    auto* root = xml->getChildByName("BLACKLIST");
    if (root == nullptr)
        return;

    for (int i = 0; i < root->getNumChildElements(); ++i)
    {
        auto* el = root->getChildElement(i);
        if (el != nullptr && el->hasTagName("PLUGIN"))
        {
            juce::String id = el->getStringAttribute("id");
            if (id.isNotEmpty())
                blacklistedIDs.push_back(id);
        }
    }
}

void PluginManager::saveBlacklist()
{
    blacklistFile.getParentDirectory().createDirectory();

    juce::XmlElement root("BLACKLIST");
    for (const auto& id : blacklistedIDs)
    {
        auto* el = root.createNewChildElement("PLUGIN");
        el->setAttribute("id", id);
    }
    root.writeTo(blacklistFile, {});
}

} // namespace HDAW
