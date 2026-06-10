#include "PluginManager.h"

namespace HDAW {

PluginManager::PluginManager()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    cacheFile = appData.getChildFile("HDAW").getChildFile("plugin_cache.xml");
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
            for (auto& desc : types)
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
#elif JUCE_MAC
    defaultDirs.add("/Library/Audio/Plug-Ins/VST3");
    defaultDirs.add("~/Library/Audio/Plug-Ins/VST3");
#elif JUCE_LINUX
    defaultDirs.add("/usr/lib/vst3");
    defaultDirs.add("/usr/local/lib/vst3");
    defaultDirs.add("~/.vst3");
#endif

    for (auto& format : formatManager.getFormats())
    {
        for (auto& dir : defaultDirs)
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
    for (auto& desc : types)
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
    return formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMessage);
}

} // namespace HDAW
