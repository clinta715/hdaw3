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

juce::StringArray PluginManager::getVst3Dirs()
{
    juce::StringArray dirs;
#if JUCE_WINDOWS
    auto programsDir = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    dirs.add(programsDir.getChildFile("Common Files\\VST3").getFullPathName());
#elif JUCE_MAC
    dirs.add("/Library/Audio/Plug-Ins/VST3");
    dirs.add("~/Library/Audio/Plug-Ins/VST3");
#elif JUCE_LINUX
    dirs.add("/usr/lib/vst3");
    dirs.add("/usr/local/lib/vst3");
    dirs.add("~/.vst3");
#endif
    return dirs;
}

juce::StringArray PluginManager::getClapDirs()
{
    juce::StringArray dirs;
#if JUCE_WINDOWS
    auto programsDir = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    dirs.add(programsDir.getChildFile("Common Files\\CLAP").getFullPathName());
#elif JUCE_MAC
    dirs.add("/Library/Audio/Plug-Ins/CLAP");
    dirs.add("~/Library/Audio/Plug-Ins/CLAP");
#elif JUCE_LINUX
    dirs.add("/usr/lib/clap");
    dirs.add("/usr/local/lib/clap");
    dirs.add("~/.clap");
#endif
    return dirs;
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
    juce::Logger::writeToLog("PluginManager: found " + juce::String(pluginFiles.size()) + " plugin files to scan");
    juce::Logger::writeToLog("PluginManager: scannerExePath=" + scannerExePath.getFullPathName() + " exists=" + (scannerExePath.existsAsFile() ? "yes" : "no"));

    for (const auto& file : pluginFiles)
    {
        if (abortRequested.load()) break;

        auto path = file.getFullPathName();
        juce::Logger::writeToLog("PluginManager: scanning " + path);

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
            juce::Logger::writeToLog("PluginManager: scan result for " + file.getFileName()
                + " ok=" + (scanResult.ok ? "true" : "false")
                + " name='" + scanResult.name + "'"
                + " error='" + scanResult.error + "'");

            if (scanResult.ok)
            {
                juce::PluginDescription desc;
                desc.name = scanResult.name;
                desc.manufacturerName = scanResult.manufacturer;
                desc.category = scanResult.category;
                desc.pluginFormatName = scanResult.format;
                desc.fileOrIdentifier = scanResult.file.isNotEmpty() ? scanResult.file : path;
                desc.uniqueId = scanResult.uid;
                desc.isInstrument = scanResult.isInstrument;
                knownPluginList.addType(desc);

                juce::Logger::writeToLog("PluginManager: found (isolated) - "
                                         + (scanResult.name.isNotEmpty() ? scanResult.name : path));
            }
            else if (scanResult.error == "Scanner timed out (30s)" ||
                     scanResult.error.startsWith("Scanner exited with code"))
            {
                if (pedalFile.existsAsFile())
                {
                    // Scanner crashed — blacklist as crash
                    auto crashedPath = pedalFile.loadFileAsString().trim();
                    if (crashedPath.isNotEmpty())
                    {
                        blacklistPlugin(crashedPath, "crash");
                        lastScanCrashCount++;
                        juce::Logger::writeToLog(
                            "PluginManager: CRASHED (isolated) and blacklisted: " + crashedPath);
                        if (progressCb)
                            progressCb("CRASHED: " + juce::File(crashedPath).getFileName(), ++completed, 0);
                    }
                    pedalFile.deleteFile();
                }
                else if (scanResult.error.startsWith("Scanner exited with code"))
                {
                    // Scanner ran but plugin failed to load (e.g. exit code 1).
                    // Blacklist as scan_failure to avoid re-scanning every startup.
                    blacklistPlugin(path, "scan_failure");
                    juce::Logger::writeToLog(
                        "PluginManager: scan failed (isolated), blacklisted: " + path
                        + " - " + scanResult.error);
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
            // Fallback: in-process scanning. Used when the isolated scanner
            // exe is unavailable (e.g. missing from a packaged install).
            //
            // NOTE: the previous implementation tried createPluginInstance()
            // first with a probeDesc that had only fileOrIdentifier set — no
            // pluginFormatName, no uid — so JUCE silently returned nullptr
            // for every plugin and the scan produced zero results. The
            // correct API for "scan a file and discover what it contains"
            // is AudioPluginFormat::findAllTypesForFile(), which populates
            // a OwnedArray<PluginDescription> with full metadata (name,
            // manufacturer, format, uid). We call that directly and only
            // instantiate if we need to (we don't — the descriptions are
            // already complete). This path is wrapped in SEH on Windows
            // so a misbehaving plugin's access violation doesn't take down
            // the host.
#if JUCE_WINDOWS
            auto oldTranslator = _set_se_translator(sehPluginCrashTranslator);
            bool crashed = false;
            try
            {
                for (auto* fmt : formatManager.getFormats())
                {
                    if (!fmt->fileMightContainThisPluginType(path))
                        continue;

                    juce::OwnedArray<juce::PluginDescription> types;
                    fmt->findAllTypesForFile(types, path);
                    if (types.isEmpty())
                        continue;

                    for (auto* t : types)
                        knownPluginList.addType(*t);

                    juce::Logger::writeToLog("PluginManager: found (in-process) - "
                                             + (types.getFirst()->name.isNotEmpty()
                                                ? types.getFirst()->name
                                                : path));
                }
            }
            catch (const std::runtime_error&)
            {
                crashed = true;
            }
            _set_se_translator(oldTranslator);

            if (crashed)
            {
                blacklistPlugin(path, "crash");
                lastScanCrashCount++;
                juce::Logger::writeToLog(
                    "PluginManager: CRASHED (in-process) and blacklisted: " + path);
                if (progressCb)
                    progressCb("CRASHED: " + file.getFileName(), ++completed, 0);
            }
#else
            // Non-Windows: no SEH, just probe.
            for (auto* fmt : formatManager.getFormats())
            {
                if (!fmt->fileMightContainThisPluginType(path))
                    continue;

                juce::OwnedArray<juce::PluginDescription> types;
                fmt->findAllTypesForFile(types, path);
                if (types.isEmpty())
                    continue;

                for (auto* t : types)
                    knownPluginList.addType(*t);

                juce::Logger::writeToLog("PluginManager: found (in-process) - "
                                         + (types.getFirst()->name.isNotEmpty()
                                            ? types.getFirst()->name
                                            : path));
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

std::vector<juce::PluginDescription> PluginManager::getInstrumentPlugins() const
{
    std::vector<juce::PluginDescription> result;
    for (const auto& desc : knownPlugins)
    {
        if (isBlacklisted(desc.fileOrIdentifier))
            continue;
        if (desc.isInstrument)
            result.push_back(desc);
    }
    return result;
}

std::vector<juce::PluginDescription> PluginManager::getEffectPlugins() const
{
    std::vector<juce::PluginDescription> result;
    for (const auto& desc : knownPlugins)
    {
        if (isBlacklisted(desc.fileOrIdentifier))
            continue;
        if (!desc.isInstrument)
            result.push_back(desc);
    }
    return result;
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
    if (!child.start(cmd, juce::ChildProcess::wantStdOut))
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
            result.uid = obj->hasProperty("uid") ? static_cast<int>(obj->getProperty("uid")) : 0;
            result.isInstrument = obj->hasProperty("isInstrument") && static_cast<bool>(obj->getProperty("isInstrument"));
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
        blacklistPlugin(desc.fileOrIdentifier, "crash");
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

void PluginManager::blacklistPlugin(const juce::String& pluginID, const juce::String& reason)
{
    if (!isBlacklisted(pluginID))
    {
        blacklistedIDs.push_back(pluginID);
        blacklistReasons[pluginID] = reason;
        saveBlacklist();
    }
}

juce::String PluginManager::getBlacklistReason(const juce::String& pluginID) const
{
    auto it = blacklistReasons.find(pluginID);
    return it != blacklistReasons.end() ? it->second : juce::String();
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
            {
                blacklistedIDs.push_back(id);
                auto reason = el->getStringAttribute("reason");
                if (reason.isNotEmpty())
                    blacklistReasons[id] = reason;
            }
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
        auto it = blacklistReasons.find(id);
        if (it != blacklistReasons.end())
            el->setAttribute("reason", it->second);
    }
    root.writeTo(blacklistFile, {});
}

} // namespace HDAW
