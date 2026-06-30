#include "PluginManager.h"
#include <stdexcept>

#if HDAW_PLUGIN_ISOLATION
#include "proxy/PluginProxySlot.h"
#include "proxy/ProxyProcessManager.h"
namespace {
    proxy::ProxyProcessManager proxyProcessManager;
}
#endif

#if JUCE_WINDOWS
#include <windows.h>

namespace {
// __try/__except must live in its own function with no local C++ objects
// (C2712 restriction: cannot mix SEH with C++ object unwinding in the same function).
bool scanNextFileSafe(juce::PluginDirectoryScanner& scanner, juce::String& name, bool& crashed)
{
    crashed = false;
    __try {
        return scanner.scanNextFile(false, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
        return false;
    }
}
} // anonymous namespace

// SEH-to-C++ exception translator. Registered with _set_se_translator before calling
// into buggy VST3 code. Must NOT return — throws a C++ exception instead.
void __cdecl sehPluginCrashTranslator(unsigned int, struct _EXCEPTION_POINTERS*)
{
    throw std::runtime_error("Plugin crashed during instantiation");
}
#endif

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

void PluginManager::scanAll(ScanProgressCallback progressCb)
{
    if (scanning.load()) return;
    scanning.store(true);
    abortRequested.store(false);

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

    auto hdawDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HDAW");
    int completed = 0;

    for (const auto& format : formatManager.getFormats())
    {
        for (const auto& dir : defaultDirs)
        {
            juce::File d(dir);
            if (!d.isDirectory()) continue;

            auto pedalName = juce::String("deadmanspedal_")
                + juce::String(format->getName()) + "_"
                + d.getFileName() + ".tmp";
            auto deadMansPedal = hdawDir.getChildFile(pedalName);

            juce::PluginDirectoryScanner scanner(
                knownPluginList, *format,
                juce::FileSearchPath(d.getFullPathName()),
                false, deadMansPedal, false
            );

            juce::String name;
            while (true)
            {
                bool ok = false;
#if JUCE_WINDOWS
                bool crashed = false;
                ok = scanNextFileSafe(scanner, name, crashed);

                if (crashed)
                {
                    if (deadMansPedal.existsAsFile())
                    {
                        auto crashedFile = deadMansPedal.loadFileAsString().trim();
                        if (crashedFile.isNotEmpty())
                        {
                            blacklistPlugin(crashedFile);
                            juce::Logger::writeToLog(
                                "PluginManager: CRASHED and blacklisted: "
                                + crashedFile);
                            if (progressCb)
                                progressCb(crashedFile, ++completed, 0);
                        }
                        deadMansPedal.deleteFile();
                    }
                    ok = false;
                }
#else
                ok = scanner.scanNextFile(false, name);
#endif
                if (!ok) break;

                if (abortRequested.load()) break;

                juce::Logger::writeToLog("PluginManager: found - " + name);
                if (progressCb)
                    progressCb(name, ++completed, 0);
            }

            if (deadMansPedal.existsAsFile())
                deadMansPedal.deleteFile();

            if (abortRequested.load()) break;
        }

        if (abortRequested.load()) break;
    }

    if (abortRequested.load())
    {
        scanning.store(false);
        return;
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
    double sampleRate, int blockSize, bool isolated)
{
    if (isBlacklisted(desc.fileOrIdentifier))
    {
        errorMessage = "Plugin is blacklisted: " + desc.fileOrIdentifier;
        return nullptr;
    }

#if HDAW_PLUGIN_ISOLATION
    if (isolated)
    {
        auto slotId = static_cast<uint32_t>(knownPlugins.size());
        auto* proxy = new proxy::PluginProxySlot(
            proxyProcessManager, slotId, desc.name);

        if (!proxyProcessManager.spawnPluginHost(
                desc.fileOrIdentifier.toStdString(), slotId))
        {
            delete proxy;
            errorMessage = "Failed to spawn isolated plugin process";
            return nullptr;
        }

        return std::unique_ptr<juce::AudioPluginInstance>(proxy);
    }
#endif

    bool crashed = false;
    std::unique_ptr<juce::AudioPluginInstance> result;

#if JUCE_WINDOWS
    auto oldTranslator = _set_se_translator(sehPluginCrashTranslator);
    try
    {
        result = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMessage);
    }
    catch (const std::runtime_error&)
    {
        errorMessage = "Plugin crashed during instantiation";
        crashed = true;
    }
    _set_se_translator(oldTranslator);
#else
    result = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMessage);
#endif

    if (crashed)
    {
        blacklistPlugin(desc.fileOrIdentifier);
        juce::Logger::writeToLog(
            "HDAW: Plugin crashed during instantiation, blacklisted: "
            + desc.fileOrIdentifier);
    }

    return result;
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
