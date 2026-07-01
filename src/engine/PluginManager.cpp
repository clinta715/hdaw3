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
    lastScanCrashCount = 0;

    loadCache();

    // Locate the scanner exe next to the main executable
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    scannerExePath = exeDir.getChildFile("hdaw_plugin_scanner.exe");

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

    auto hdawDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    auto pedalFile = hdawDir.getChildFile("deadmanspedal_scan.tmp");
    int completed = 0;

    // Enumerate all candidate plugin files
    auto pluginFiles = findPluginFiles(defaultDirs);

    for (const auto& file : pluginFiles)
    {
        if (abortRequested.load()) break;

        auto path = file.getFullPathName();

        // Skip if already known
        bool alreadyKnown = false;
        for (const auto& desc : knownPluginList.getTypes())
        {
            if (desc.fileOrIdentifier == path)
            {
                alreadyKnown = true;
                break;
            }
        }
        if (alreadyKnown) continue;

        // Skip if blacklisted
        if (isBlacklisted(path)) continue;

        if (progressCb)
            progressCb(file.getFileName(), completed, 0);

        // Check if scanner exe exists; fall back to in-process if not
        if (scannerExePath.existsAsFile())
        {
            auto scanResult = scanPluginIsolated(path);

            if (scanResult.ok)
            {
                // Add to known list
                juce::PluginDescription desc;
                desc.name = scanResult.name;
                desc.manufacturerName = scanResult.manufacturer;
                desc.category = scanResult.category;
                desc.pluginFormatName = scanResult.format;
                desc.fileOrIdentifier = scanResult.file.isNotEmpty() ? scanResult.file : path;
                knownPluginList.addType(desc);

                juce::Logger::writeToLog("PluginManager: found (isolated) - " + scanResult.name);
            }
            else if (scanResult.error == "Scanner timed out (30s)" ||
                     scanResult.error.startsWith("Scanner exited with code"))
            {
                // Crash or timeout — read pedal and blacklist
                if (pedalFile.existsAsFile())
                {
                    auto crashedPath = pedalFile.loadFileAsString().trim();
                    if (crashedPath.isNotEmpty())
                    {
                        blacklistPlugin(crashedPath);
                        lastScanCrashCount++;
                        juce::Logger::writeToLog(
                            "PluginManager: CRASHED (isolated) and blacklisted: " + crashedPath);
                        if (progressCb)
                            progressCb("CRASHED: " + juce::File(crashedPath).getFileName(), ++completed, 0);
                    }
                    pedalFile.deleteFile();
                }
            }
            else
            {
                // Normal load failure — skip
                juce::Logger::writeToLog("PluginManager: failed to load (isolated): " + path
                                         + " - " + scanResult.error);
            }
        }
        else
        {
            // Fallback: in-process scanning with SEH (existing code)
#if JUCE_WINDOWS
            auto oldTranslator = _set_se_translator(sehPluginCrashTranslator);
            bool crashed = false;
            try
            {
                for (auto* fmt : formatManager.getFormats())
                {
                    if (!fmt->fileMightContainThisPluginType(path))
                        continue;

                    juce::String error;
                    juce::PluginDescription probeDesc;
                    probeDesc.fileOrIdentifier = path;
                    auto instance = formatManager.createPluginInstance(
                        probeDesc, 44100.0, 512, error);
                    if (instance)
                    {
                        juce::PluginDescription desc;
                        desc.fileOrIdentifier = path;
                        desc.pluginFormatName = fmt->getName();
                        // findAllTypesForFile to get full metadata
                        juce::OwnedArray<juce::PluginDescription> types;
                        fmt->findAllTypesForFile(types, path);
                        if (!types.isEmpty())
                            knownPluginList.addType(*types[0]);
                        else
                            knownPluginList.addType(desc);

                        juce::Logger::writeToLog("PluginManager: found (in-process) - " + path);
                    }
                }
            }
            catch (const std::runtime_error&)
            {
                crashed = true;
            }
            _set_se_translator(oldTranslator);

            if (crashed)
            {
                blacklistPlugin(path);
                lastScanCrashCount++;
                juce::Logger::writeToLog(
                    "PluginManager: CRASHED (in-process) and blacklisted: " + path);
                if (progressCb)
                    progressCb("CRASHED: " + file.getFileName(), ++completed, 0);
            }
#else
            // Non-Windows: no SEH, just try loading
            for (auto* fmt : formatManager.getFormats())
            {
                if (!fmt->fileMightContainThisPluginType(path))
                    continue;
                juce::String error;
                juce::PluginDescription probeDesc;
                probeDesc.fileOrIdentifier = path;
                auto instance = formatManager.createPluginInstance(
                    probeDesc, 44100.0, 512, error);
                if (instance)
                {
                    juce::OwnedArray<juce::PluginDescription> types;
                    fmt->findAllTypesForFile(types, path);
                    if (!types.isEmpty())
                        knownPluginList.addType(*types[0]);
                    juce::Logger::writeToLog("PluginManager: found (in-process) - " + path);
                }
            }
#endif
        }

        completed++;
        if (progressCb)
            progressCb(file.getFileName(), completed, 0);
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

juce::Array<juce::File> PluginManager::findPluginFiles(const juce::StringArray& dirs)
{
    juce::Array<juce::File> result;
    for (const auto& dir : dirs)
    {
        juce::File d(dir);
        if (!d.isDirectory()) continue;

        // VST3 files/bundles
        d.findChildFiles(result, juce::File::findFiles, false, "*.vst3");
        // CLAP files
        d.findChildFiles(result, juce::File::findFiles, false, "*.clap");
    }
    return result;
}

PluginManager::ScanResult PluginManager::scanPluginIsolated(const juce::String& pluginPath)
{
    ScanResult result{};
    result.ok = false;

    if (!scannerExePath.existsAsFile())
    {
        result.error = "Scanner exe not found: " + scannerExePath.getFullPathName();
        return result;
    }

    auto hdawDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    auto pedalFile = hdawDir.getChildFile("deadmanspedal_scan.tmp");

    // Write pedal BEFORE spawn
    pedalFile.replaceWithText(pluginPath);

    // Build command line
    auto cmd = "\"" + scannerExePath.getFullPathName() + "\""
             + " --plugin=\"" + pluginPath + "\""
             + " --pedal-file=\"" + pedalFile.getFullPathName() + "\"";

    // Spawn child process
    juce::ChildProcess child;
    if (!child.start(cmd, 0))
    {
        result.error = "Failed to start scanner process";
        pedalFile.deleteFile();
        return result;
    }

    // Wait up to 30 seconds
    bool finished = child.waitForProcessToFinish(30000);
    auto output = child.readAllProcessOutput();
    int exitCode = child.getExitCode();

    if (!finished)
    {
        // Timeout — kill the child
        child.kill();
        result.error = "Scanner timed out (30s)";
        // Pedal file still has the plugin path — caller will read it
        return result;
    }

    // Only clear pedal on normal exit (success or load failure).
    // Leave it intact for crash exit codes so scanAll() can read it.
    if (exitCode == 0 || exitCode == 1)
        pedalFile.deleteFile();

    if (exitCode == 0 && output.isNotEmpty())
    {
        // Parse JSON output
        auto json = juce::JSON::parse(output);
        if (auto* obj = json.getDynamicObject())
        {
            result.ok = obj->hasProperty("ok") && static_cast<bool>(obj->getProperty("ok"));
            result.name = obj->getProperty("name").toString();
            result.manufacturer = obj->getProperty("manufacturer").toString();
            result.category = obj->getProperty("category").toString();
            result.format = obj->getProperty("format").toString();
            result.file = obj->getProperty("file").toString();
            result.id = obj->getProperty("id").toString();
            result.error = obj->getProperty("error").toString();
        }
    }
    else
    {
        result.error = "Scanner exited with code " + juce::String(exitCode);
    }

    return result;
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
