#include "PluginManager.h"
#include "../common/DebugLog.h"
#include "../frontend/FrontendServer.h"
#include "../frontend/FrontendRpc.h"
#include <stdexcept>
#include <QJsonObject>
#include <QString>

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
    presetCacheFile = hdawDir.getChildFile("preset_cache.xml");

    loadBlacklist();
    loadCache();

#if HDAW_PLUGIN_ISOLATION
    crashRecovery = std::make_unique<CrashRecoveryManager>();
    crashRecovery->setRespawnFn([this](uint32_t oldSlotId, const juce::String& pluginPath) -> bool {
        return respawnIsolatedSlot(oldSlotId, pluginPath);
    });
    crashRecovery->setGiveUpFn([](uint32_t slotId, const juce::String& name) {
        juce::Logger::writeToLog("CrashRecovery: gave up on slot " + juce::String((int)slotId));
    });
    startTimer(250);
#endif
}

PluginManager::~PluginManager()
{
    stopTimer();
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
    loadPresetCache();
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

                if (scanResult.numPrograms > 1)
                {
                    PluginPresetInfo info;
                    info.numPrograms = scanResult.numPrograms;
                    info.programNames = scanResult.programNames;
                    auto id = desc.createIdentifierString();
                    presetCache[id] = info;
                    juce::Logger::writeToLog("PluginManager: cached " + juce::String(scanResult.numPrograms)
                                             + " presets for " + scanResult.name);
                }

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
    savePresetCache();
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
            result.numPrograms = obj->hasProperty("numPrograms") ? static_cast<int>(obj->getProperty("numPrograms")) : 0;
            if (obj->hasProperty("programNames"))
                result.programNames = juce::StringArray::fromTokens(obj->getProperty("programNames").toString(), "\x01", "");
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

    lastSampleRate = sampleRate;
    lastBlockSize = blockSize;

#if HDAW_PLUGIN_ISOLATION
    if (isolated || isolationEnabled)
    {
        auto slotId = nextProxySlotId.fetch_add(1, std::memory_order_relaxed);

        auto resolvedDesc = resolveIdentifierToPath(desc, knownPluginList);

        if (!proxyProcessManager.spawnPluginHost(
                resolvedDesc.fileOrIdentifier.toStdString(), slotId))
        {
            errorMessage = "Failed to spawn isolated plugin process";
            return nullptr;
        }

        auto* proxy = new proxy::PluginProxySlot(
            proxyProcessManager, slotId, desc.name, resolvedDesc.fileOrIdentifier);

        proxyProcessManager.setSlotCrashCallback(slotId,
            [proxy](uint32_t id) { proxy->onChildCrashed(); });

        proxy->setCrashRecoveryNotifier(
            [this](uint32_t sid, const juce::String& name, const juce::String& path) {
                if (crashRecovery) crashRecovery->onSlotCrashed(sid, name, path);
                if (auto* srv = frontend::FrontendServer::instance()) {
                    QJsonObject payload{
                        { "trackIndex", slotTrackIndex(sid) },
                        { "pluginName", QString::fromUtf8(name.toRawUTF8()) },
                        { "pluginId",   QString::fromUtf8(path.toRawUTF8()) },
                    };
                    srv->broadcastNotificationFromAnyThread(frontend::notify::PluginCrashed, payload);
                }
            });
        proxy->setRespawnRequestFn(
            [this](uint32_t sid) {
                if (crashRecovery) crashRecovery->requestRespawn(sid, true);
            });

        liveProxySlots[slotId] = proxy;

        proxyProcessManager.startHealthMonitor(2000);

        return std::unique_ptr<juce::AudioPluginInstance>(proxy);
    }
#endif

    // The AudioPluginFormatManager's findFormatForDescription only matches a
    // format when desc.fileOrIdentifier ends with that format's file extension
    // (e.g. CLAPPluginFormat::fileMightContainThisPluginType requires ".clap").
    // Track::rebuildFXChain passes the plugin *identifier string* (e.g.
    // "CLAP-Vital-aaca468a-0") here, which matches no format. Resolve it back to
    // the real plugin file path recorded during the scan so the in-process
    // (non-isolated) render path can load the plugin.
    auto resolvedDesc = resolveIdentifierToPath(desc, knownPluginList);

    bool crashed = false;
    std::unique_ptr<juce::AudioPluginInstance> result;

#if JUCE_WINDOWS
    auto oldTranslator = _set_se_translator(sehPluginCrashTranslator);
    try
    {
        result = formatManager.createPluginInstance(resolvedDesc, sampleRate, blockSize, errorMessage);
    }
    catch (const std::runtime_error&)
    {
        errorMessage = "Plugin crashed during instantiation";
        crashed = true;
    }
    _set_se_translator(oldTranslator);
#else
    result = formatManager.createPluginInstance(resolvedDesc, sampleRate, blockSize, errorMessage);
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

juce::PluginDescription PluginManager::resolveIdentifierToPath(
    const juce::PluginDescription& desc,
    const juce::KnownPluginList& knownList)
{
    auto resolved = desc;
    auto lower = resolved.fileOrIdentifier.toLowerCase();
    if (!lower.endsWith(".clap") && !lower.endsWith(".vst3"))
    {
        for (const auto& kd : knownList.getTypes())
        {
            if (kd.matchesIdentifierString(resolved.fileOrIdentifier))
            {
                resolved.fileOrIdentifier = kd.fileOrIdentifier;
                if (resolved.name.isEmpty())
                    resolved.name = kd.name;
                return resolved;
            }
        }

        // Fallback: match by format+name when the identifier hash doesn't
        // match (e.g. the plugin was rescanned or the path changed).
        for (const auto& kd : knownList.getTypes())
        {
            if (kd.pluginFormatName == resolved.pluginFormatName
                && kd.name == resolved.name)
            {
                resolved.fileOrIdentifier = kd.fileOrIdentifier;
                return resolved;
            }
        }
    }
    return resolved;
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

void PluginManager::timerCallback()
{
#if HDAW_PLUGIN_ISOLATION
    if (crashRecovery) crashRecovery->tick();
#endif
}

bool PluginManager::respawnIsolatedSlot(uint32_t oldSlotId, const juce::String& pluginPath)
{
#if HDAW_PLUGIN_ISOLATION
    HDAW_LOG("CrashRecovery", juce::String("respawnIsolatedSlot: oldSlot=") + juce::String((int)oldSlotId) + " path=" + pluginPath);

    if (pluginPath.isEmpty()) {
        HDAW_LOG("CrashRecovery", juce::String("respawnIsolatedSlot: empty plugin path for slot ") + juce::String((int)oldSlotId) + ", refusing to spawn");
        return false;
    }

    auto it = liveProxySlots.find(oldSlotId);
    if (it == liveProxySlots.end() || it->second == nullptr) return false;

    auto* proxy = it->second;

    proxyProcessManager.killPluginHost(oldSlotId, proxy::KillMode::KillHard);

    auto newSlotId = nextProxySlotId.fetch_add(1, std::memory_order_relaxed);

    if (!proxyProcessManager.spawnPluginHost(pluginPath.toStdString(), newSlotId))
        return false;

    proxyProcessManager.setSlotCrashCallback(newSlotId,
        [proxy](uint32_t) { proxy->onChildCrashed(); });

    auto* rawShm = proxyProcessManager.getShm(newSlotId);
    if (!rawShm) return false;

    auto newShm = std::shared_ptr<proxy::ShmRegion>(rawShm, [](proxy::ShmRegion*){});

    // Acquire graphLock to synchronize with the audio thread. The audio
    // callback does tryEnter(graphLock) in MainAudioProcessor::processBlock;
    // if it fails, it skips graph processing (returns silence). This ensures
    // migrateToNewSlot's shared_ptr assignment doesn't race with the audio
    // thread reading shmHandle.
    if (graphLockPtr) graphLockPtr->enter();
    proxy->migrateToNewSlot(newSlotId, newShm);
    if (graphLockPtr) graphLockPtr->exit();

    auto stateBlock = proxy::PluginProxySlot::loadStateForOldSlotId(oldSlotId);
    if (stateBlock.getSize() > 0)
        proxy->setStateInformation(stateBlock.getData(), (int)stateBlock.getSize());

    proxy->prepareToPlay(lastSampleRate, lastBlockSize);

    liveProxySlots.erase(oldSlotId);
    liveProxySlots[newSlotId] = proxy;

    registerSlotTrackIndex(newSlotId, slotTrackIndex(oldSlotId));
    if (auto* srv = frontend::FrontendServer::instance()) {
        QJsonObject payload{
            { "trackIndex", slotTrackIndex(newSlotId) },
            { "pluginId",   QString::fromUtf8(pluginPath.toRawUTF8()) },
        };
        srv->broadcastNotificationFromAnyThread(frontend::notify::PluginRecovered, payload);
    }

    proxy::PluginProxySlot::clearStateForSlotId(oldSlotId);

    return true;
#else
    return false;
#endif
}

void PluginManager::killProxyForTesting(uint32_t slotId)
{
#if HDAW_PLUGIN_ISOLATION
    auto* info = proxyProcessManager.getChildInfo(slotId);
    if (info && info->processHandle != INVALID_HANDLE_VALUE)
        TerminateProcess(info->processHandle, 0);
#endif
}

void PluginManager::registerSlotTrackIndex(uint32_t slotId, int trackIndex)
{
    std::lock_guard<std::mutex> lock(slotTrackMutex_);
    slotTrackIndex_[slotId] = trackIndex;
}

int PluginManager::slotTrackIndex(uint32_t slotId) const
{
    std::lock_guard<std::mutex> lock(slotTrackMutex_);
    auto it = slotTrackIndex_.find(slotId);
    return it != slotTrackIndex_.end() ? it->second : -1;
}

void PluginManager::loadPresetCache()
{
    if (!presetCacheFile.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(presetCacheFile);
    if (!xml) return;
    for (int i = 0; i < xml->getNumChildElements(); ++i)
    {
        auto* child = xml->getChildElement(i);
        if (child && child->getTagName() == "PLUGIN")
        {
            PluginPresetInfo info;
            info.numPrograms = child->getIntAttribute("numPrograms", 0);
            auto namesStr = child->getStringAttribute("programNames", {});
            if (namesStr.isNotEmpty())
                info.programNames = juce::StringArray::fromTokens(namesStr, "\x01", {});
            auto id = child->getStringAttribute("id", {});
            if (id.isNotEmpty())
                presetCache[id] = info;
        }
    }
}

void PluginManager::savePresetCache()
{
    presetCacheFile.getParentDirectory().createDirectory();
    juce::XmlElement root("PRESET_CACHE");
    for (const auto& [id, info] : presetCache)
    {
        auto* child = root.createNewChildElement("PLUGIN");
        child->setAttribute("id", id);
        child->setAttribute("numPrograms", info.numPrograms);
        if (!info.programNames.isEmpty())
            child->setAttribute("programNames", info.programNames.joinIntoString("\x01"));
    }
    root.writeTo(presetCacheFile, {});
}

const PluginPresetInfo* PluginManager::getPresetInfo(const juce::String& pluginId) const
{
    auto it = presetCache.find(pluginId);
    return it != presetCache.end() ? &it->second : nullptr;
}

} // namespace HDAW
