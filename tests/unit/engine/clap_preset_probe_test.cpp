#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "engine/CLAPPluginFormat.h"
#include "common/DebugLog.h"

#include <clap/factory/preset-discovery.h>
#include <clap/ext/preset-load.h>
#include <clap/ext/thread-check.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <stdexcept>
#endif

// Phase-0 capability probe (docs/plans/2026-08-19-clap-preset-capability-probe.md).
// Measurement-only: no capability assertions beyond structural sanity.

namespace {

const char* kTag = "clap_preset_probe";

bool clapPresetProbeEnabled()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    return s.trim().isNotEmpty() && s.trim() != "0";
}

void logBoth(const std::string& msg)
{
    HDAW_LOG(kTag, msg);
    std::cout << "[clap_preset_probe] " << msg << std::endl;
}

struct PluginInfo
{
    std::string name;
    std::string id;
};

struct ModuleResult
{
    std::string fileName;
    std::string path;
    bool loaded = false;
    bool hasDiscoveryFactory = false;
    std::vector<PluginInfo> plugins;
    std::map<std::string, std::string> presetLoad;    // plugin id -> "y"/"n"/"n/a"
    std::map<std::string, std::string> presetLoadCompat;
    std::map<std::string, int> presetCount;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> samples;
    std::map<std::string, std::string> notes;         // per-plugin failures/skips
    std::string moduleNote;                           // module-level failure/skip
};

// Tier 2 runs on plugins from factory-present modules plus this shortlist
// (case-insensitive substring match on the descriptor name).
const char* kShortlist[] = {
    "TyrellN6", "Diva", "Zebralette3", "Surge XT", "Vital",
    "Odin2", "Dexed", "JC303", "Xenia", "Altitude",
};

bool isShortlisted(const std::string& name)
{
    juce::String n(juce::String::fromUTF8(name.c_str(), (int) name.size()));
    n = n.toLowerCase();
    for (const auto* s : kShortlist)
        if (n.contains(juce::String(s).toLowerCase()))
            return true;
    return false;
}

std::vector<juce::String> collectClapFiles()
{
    CLAPPluginFormat format;
    auto paths = format.getDefaultLocationsToSearch();
    auto found = format.searchPathsForPlugins(paths, true, false);
    std::vector<juce::String> files;
    for (const auto& f : found)
        if (std::find(files.begin(), files.end(), f) == files.end())
            files.push_back(f);
    return files;
}

// ── Minimal static probe host (Tier 2) ─────────────────────────
// Deliberately NOT CLAPHost/CLAPPluginInstance: the probe must stay
// decoupled from production classes. Provides only the thread-check
// extension, reporting the probe thread as the CLAP main thread.

std::thread::id g_probeThreadId;

bool CLAP_ABI probeIsMainThread(const clap_host_t*)
{
    return std::this_thread::get_id() == g_probeThreadId;
}

bool CLAP_ABI probeIsAudioThread(const clap_host_t*)
{
    return false;
}

const clap_host_thread_check_t g_probeThreadCheck = {
    &probeIsMainThread,
    &probeIsAudioThread,
};

const void* CLAP_ABI probeHostGetExtension(const clap_host_t*, const char* id)
{
    if (id != nullptr && std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0)
        return &g_probeThreadCheck;
    return nullptr;
}

void CLAP_ABI probeHostNoop(const clap_host_t*) {}

const clap_host_t g_probeHost = {
    CLAP_VERSION_INIT,
    nullptr,            // host_data
    "HDAW probe",
    "HDAW",
    "",
    "0.23.1",
    &probeHostGetExtension,
    &probeHostNoop,     // request_restart
    &probeHostNoop,     // request_process
    &probeHostNoop,     // request_callback
};

struct PresetRecord
{
    std::string name;
    std::string loadKey;
    std::vector<std::pair<std::string, std::string>> pluginIds; // (abi, id)
};

struct ReceiverState
{
    std::vector<std::string> pluginIds; // all clap ids of the module
    std::vector<PresetRecord> records;
    bool hasPending = false;
    PresetRecord pending;
    int errorCount = 0;

    void commitPending()
    {
        if (hasPending)
        {
            records.push_back(std::move(pending));
            pending = {};
            hasPending = false;
        }
    }
};

ReceiverState* receiverStateOf(const clap_preset_discovery_metadata_receiver_t* r)
{
    return static_cast<ReceiverState*>(r->receiver_data);
}

// ── Metadata receiver (Tier 3) ─────────────────────────────────

void CLAP_ABI recvOnError(const clap_preset_discovery_metadata_receiver_t* r,
                          int32_t osError, const char* msg)
{
    auto* st = receiverStateOf(r);
    ++st->errorCount;
    if (st->errorCount <= 3)
        logBoth(std::string("receiver on_error: ") + (msg ? msg : "")
                + " os=" + std::to_string(osError));
}

bool CLAP_ABI recvBeginPreset(const clap_preset_discovery_metadata_receiver_t* r,
                              const char* name, const char* loadKey)
{
    auto* st = receiverStateOf(r);
    st->commitPending();
    st->hasPending = true;
    st->pending.name = name ? name : "";
    st->pending.loadKey = loadKey ? loadKey : "";
    return true;
}

void CLAP_ABI recvAddPluginId(const clap_preset_discovery_metadata_receiver_t* r,
                              const clap_universal_plugin_id_t* pid)
{
    if (pid == nullptr)
        return;
    auto* st = receiverStateOf(r);
    if (!st->hasPending)
        return;
    st->pending.pluginIds.emplace_back(pid->abi ? pid->abi : "",
                                       pid->id ? pid->id : "");
}

void CLAP_ABI recvNoop(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvSetFlags(const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
void CLAP_ABI recvSetTimestamps(const clap_preset_discovery_metadata_receiver_t*,
                                clap_timestamp, clap_timestamp) {}
void CLAP_ABI recvAddExtraInfo(const clap_preset_discovery_metadata_receiver_t*,
                               const char*, const char*) {}

const clap_preset_discovery_metadata_receiver_t g_metadataReceiver = {
    nullptr,            // receiver_data — set per enumeration
    &recvOnError,
    &recvBeginPreset,
    &recvAddPluginId,
    &recvNoop,          // set_soundpack_id
    &recvSetFlags,
    &recvNoop,          // add_creator
    &recvNoop,          // set_description
    &recvSetTimestamps,
    &recvNoop,          // add_feature
    &recvAddExtraInfo,
};

// ── Indexer (Tier 3) ───────────────────────────────────────────

struct IndexerState
{
    std::vector<std::string> extensions; // empty entry = match all files
    std::vector<std::pair<uint32_t, std::string>> locations; // (kind, location)
};

IndexerState* indexerStateOf(const clap_preset_discovery_indexer_t* ix)
{
    return static_cast<IndexerState*>(ix->indexer_data);
}

bool CLAP_ABI ixDeclareFiletype(const clap_preset_discovery_indexer_t* ix,
                                const clap_preset_discovery_filetype_t* ft)
{
    if (ft == nullptr)
        return false;
    auto* st = indexerStateOf(ix);
    // NULL/empty extension means "every file matches" per the spec.
    st->extensions.emplace_back(
        (ft->file_extension != nullptr && ft->file_extension[0] != '\0')
            ? std::string(ft->file_extension) : std::string());
    return true;
}

bool CLAP_ABI ixDeclareLocation(const clap_preset_discovery_indexer_t* ix,
                                const clap_preset_discovery_location_t* loc)
{
    if (loc == nullptr)
        return false;
    auto* st = indexerStateOf(ix);
    st->locations.emplace_back(loc->kind,
                               loc->location != nullptr ? std::string(loc->location)
                                                        : std::string());
    return true;
}

bool CLAP_ABI ixDeclareSoundpack(const clap_preset_discovery_indexer_t*,
                                 const clap_preset_discovery_soundpack_t*)
{
    return true;
}

const void* CLAP_ABI ixGetExtension(const clap_preset_discovery_indexer_t*, const char*)
{
    return nullptr;
}

const clap_preset_discovery_indexer_t g_indexer = {
    CLAP_VERSION_INIT,
    "HDAW",
    "HDAW",
    "",
    "0.23.1",
    nullptr,            // indexer_data — set per provider
    &ixDeclareFiletype,
    &ixDeclareLocation,
    &ixDeclareSoundpack,
    &ixGetExtension,
};

// ── Tier 2 ─────────────────────────────────────────────────────

void probePluginInit(const clap_plugin_t* plugin, bool& initOk, bool& crashed)
{
    initOk = false;
    crashed = false;
#if JUCE_WINDOWS
    auto oldTranslator = _set_se_translator([](unsigned int, struct _EXCEPTION_POINTERS*) {
        throw std::runtime_error("CLAP plugin crashed during init");
    });
    try {
        initOk = plugin->init(plugin);
    } catch (const std::runtime_error&) {
        crashed = true;
    }
    _set_se_translator(oldTranslator);
#else
    initOk = plugin->init(plugin);
#endif
}

void tier2ProbePlugin(ModuleResult& res, CLAPModule& module, const PluginInfo& info,
                      CLAPPluginFormat& format)
{
    const auto* factory = module.factory;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &g_probeHost, info.id.c_str());
    if (plugin == nullptr)
    {
        res.notes[info.id] = "create_plugin=null";
        logBoth("tier2 create_plugin failed: " + info.name + " [" + info.id + "]");
        return;
    }

    bool initOk = false;
    bool crashed = false;
    probePluginInit(plugin, initOk, crashed);

    if (!initOk)
    {
        // destroy is required even after a failed init; a mid-init SEH crash
        // leaves the instance state unknown, so skip destroy in that case.
        if (!crashed && plugin->destroy != nullptr)
            plugin->destroy(plugin);
        res.notes[info.id] = crashed ? "init SEH-crash" : "init=false";
        logBoth("tier2 init failed (" + std::string(crashed ? "SEH crash" : "false")
                + "): " + info.name + " [" + info.id + "] — JUCE fallback");

        juce::PluginDescription desc;
        desc.name = juce::String::fromUTF8(info.name.c_str(), (int) info.name.size());
        desc.fileOrIdentifier = res.path;
        desc.pluginFormatName = "CLAP";
        juce::String err;
        auto instance = format.createInstanceFromDescription(desc, 44100.0, 512, err);
        if (instance != nullptr)
            logBoth("tier2 JUCE fallback OK: " + info.name
                    + " (ext-probe line logged by CLAPPluginInstance::initialize)");
        else
            logBoth("tier2 JUCE fallback FAILED: " + info.name + ": " + err.toStdString());
        return;
    }

    const bool hasLoad = plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD) != nullptr;
    const bool hasLoadCompat = plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD_COMPAT) != nullptr;
    res.presetLoad[info.id] = hasLoad ? "y" : "n";
    res.presetLoadCompat[info.id] = hasLoadCompat ? "y" : "n";
    logBoth("tier2 preset-load: " + info.name + " [" + info.id + "] "
            + CLAP_EXT_PRESET_LOAD + std::string(hasLoad ? "=y" : "=n") + ", "
            + CLAP_EXT_PRESET_LOAD_COMPAT + std::string(hasLoadCompat ? "=y" : "=n"));

    // Destroy BEFORE the CLAPModule unloads (scope ordering matters).
    plugin->destroy(plugin);
}

// ── Tier 3 ─────────────────────────────────────────────────────

void bucketResults(ModuleResult& res, const ReceiverState& st)
{
    for (const auto& rec : st.records)
    {
        bool matchesAny = false;
        std::vector<std::string> matched;
        for (const auto& pid : rec.pluginIds)
        {
            if (pid.first != "clap")
                continue;
            for (const auto& moduleId : st.pluginIds)
            {
                if (pid.second == moduleId)
                {
                    matchesAny = true;
                    matched.push_back(moduleId);
                }
            }
        }

        // Presets with no plugin ids at all count for every contained plugin.
        if (rec.pluginIds.empty())
        {
            for (const auto& moduleId : st.pluginIds)
                matched.push_back(moduleId);
        }
        else if (!matchesAny)
        {
            continue; // belongs to a plugin not in this module
        }

        for (const auto& moduleId : matched)
        {
            ++res.presetCount[moduleId];
            auto& s = res.samples[moduleId];
            if (s.size() < 8)
                s.emplace_back(rec.name, rec.loadKey);
        }
    }
}

void enumerateFileLocation(ModuleResult& res,
                           const clap_preset_discovery_provider_t* provider,
                           const std::string& location,
                           const std::vector<std::string>& extensions,
                           ReceiverState& st)
{
    clap_preset_discovery_metadata_receiver_t receiver = g_metadataReceiver;
    receiver.receiver_data = &st;

    juce::File locFile(juce::String::fromUTF8(location.c_str(), (int) location.size()));
    if (locFile.existsAsFile())
    {
        provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                               location.c_str(), &receiver);
        return;
    }
    if (!locFile.isDirectory())
    {
        logBoth("tier3 location does not exist: " + location);
        return;
    }

    juce::StringArray patterns;
    for (const auto& ext : extensions)
        patterns.add(juce::String("*.") + juce::String::fromUTF8(ext.c_str(), (int) ext.size()));
    if (patterns.isEmpty())
        patterns.add("*"); // no filetype declared -> take all files

    int filesSent = 0;
    bool capHit = false;
    for (auto entry : juce::RangedDirectoryIterator(
             locFile, true, patterns.joinIntoString(";"), juce::File::findFiles))
    {
        if (filesSent >= 2000)
        {
            capHit = true;
            break;
        }
        const auto fullPath = entry.getFile().getFullPathName().toStdString();
        provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                               fullPath.c_str(), &receiver);
        ++filesSent;
    }
    if (capHit)
        logBoth("tier3 file cap (2000) hit at location: " + location);
    logBoth("tier3 FILE location '" + location + "': " + std::to_string(filesSent)
            + " files sent to get_metadata" + (capHit ? " (CAPPED)" : ""));
}

void tier3Enumerate(ModuleResult& res, CLAPModule& module)
{
    const auto* dfactory = static_cast<const clap_preset_discovery_factory_t*>(
        module.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (dfactory == nullptr)
        dfactory = static_cast<const clap_preset_discovery_factory_t*>(
            module.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
    if (dfactory == nullptr)
        return;

    std::vector<std::string> moduleIds;
    for (const auto& p : res.plugins)
        moduleIds.push_back(p.id);

    const uint32_t providerCount = dfactory->count(dfactory);
    logBoth("tier3 providers: " + std::to_string(providerCount) + " in " + res.fileName);

    for (uint32_t pi = 0; pi < providerCount; ++pi)
    {
        const auto* pdesc = dfactory->get_descriptor(dfactory, pi);
        if (pdesc == nullptr || pdesc->id == nullptr)
            continue;
        const std::string providerId = pdesc->id;
        const std::string providerName = pdesc->name ? pdesc->name : "";

        IndexerState ixState;
        clap_preset_discovery_indexer_t indexer = g_indexer;
        indexer.indexer_data = &ixState;

        const auto* provider = dfactory->create(dfactory, &indexer, providerId.c_str());
        if (provider == nullptr)
        {
            logBoth("tier3 provider create failed: " + providerId);
            continue;
        }

        if (!provider->init(provider))
        {
            logBoth("tier3 provider init failed: " + providerName + " [" + providerId + "]");
            provider->destroy(provider);
            continue;
        }

        logBoth("tier3 provider '" + providerName + "' [" + providerId + "]: "
                + std::to_string(ixState.locations.size()) + " location(s), "
                + std::to_string(ixState.extensions.size()) + " filetype(s)");

        ReceiverState st;
        st.pluginIds = moduleIds;

        for (const auto& loc : ixState.locations)
        {
            if (loc.first == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
            {
                clap_preset_discovery_metadata_receiver_t receiver = g_metadataReceiver;
                receiver.receiver_data = &st;
                // PLUGIN kind: presets bundled in the DSO, location must be null.
                provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                       nullptr, &receiver);
            }
            else if (loc.first == CLAP_PRESET_DISCOVERY_LOCATION_FILE)
            {
                enumerateFileLocation(res, provider, loc.second, ixState.extensions, st);
            }
        }

        st.commitPending();
        bucketResults(res, st);

        if (st.errorCount > 0)
            logBoth("tier3 receiver errors: " + std::to_string(st.errorCount)
                    + " (provider " + providerId + ")");

        provider->destroy(provider);
    }
}

} // namespace

TEST(ClapPresetProbe, ScanInstalledClaps)
{
    if (!clapPresetProbeEnabled())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set";

    g_probeThreadId = std::this_thread::get_id();

    const auto files = collectClapFiles();
    logBoth("found " + std::to_string(files.size()) + " .clap file(s)");

    CLAPPluginFormat format;
    std::vector<ModuleResult> results;
    int modulesTested = 0;

    for (const auto& path : files)
    {
        ModuleResult res;
        res.path = path.toStdString();
        res.fileName = juce::File(path).getFileName().toStdString();

        CLAPModule module;
        if (!module.load(path))
        {
            res.moduleNote = "load-failed";
            logBoth("module load FAILED: " + res.fileName);
            results.push_back(std::move(res));
            continue;
        }
        ++modulesTested;
        res.loaded = true;

        const void* df1 = module.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
        const void* df2 = module.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
        res.hasDiscoveryFactory = (df1 != nullptr) || (df2 != nullptr);

        uint32_t count = module.factory->get_plugin_count(module.factory);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* d = module.factory->get_plugin_descriptor(module.factory, i);
            if (d == nullptr || d->id == nullptr)
                continue;
            PluginInfo info;
            info.name = d->name ? d->name : "";
            info.id = d->id;
            res.plugins.push_back(info);
        }

        {
            std::string line = "module: " + res.fileName
                + " | discoveryFactory=" + (res.hasDiscoveryFactory ? "y" : "n")
                + " | plugins: ";
            for (size_t i = 0; i < res.plugins.size(); ++i)
                line += (i > 0 ? ", " : "") + res.plugins[i].name
                        + " [" + res.plugins[i].id + "]";
            logBoth(line);
        }

        // Tier 2 — factory-present modules get every plugin probed; otherwise
        // only shortlisted plugins are instantiated.
        for (const auto& info : res.plugins)
        {
            if (res.hasDiscoveryFactory || isShortlisted(info.name))
                tier2ProbePlugin(res, module, info, format);
            else
            {
                res.presetLoad[info.id] = "n/a";
                res.presetLoadCompat[info.id] = "n/a";
            }
        }

        // Tier 3 — enumerate presets where a discovery factory exists.
        if (res.hasDiscoveryFactory)
            tier3Enumerate(res, module);

        // CLAPModule unloads here — all plugin instances were destroyed above.
        results.push_back(std::move(res));
    }

    // ── Summary ────────────────────────────────────────────────
    std::cout << "\n==== CLAP preset capability summary ====\n";
    logBoth("==== summary ====");
    for (const auto& r : results)
    {
        std::string line = r.fileName;
        if (!r.loaded)
        {
            line += " | NOT LOADED (" + r.moduleNote + ")";
            std::cout << line << "\n";
            logBoth(line);
            continue;
        }
        line += std::string(" | discoveryFactory=") + (r.hasDiscoveryFactory ? "y" : "n");
        for (const auto& p : r.plugins)
        {
            line += " | " + p.name + ": preset-load=";
            auto it = r.presetLoad.find(p.id);
            line += (it != r.presetLoad.end() ? it->second : std::string("n/a"));
            auto itc = r.presetLoadCompat.find(p.id);
            line += ", preset-load.draft=";
            line += (itc != r.presetLoadCompat.end() ? itc->second : std::string("n/a"));
            auto pc = r.presetCount.find(p.id);
            if (pc != r.presetCount.end())
                line += ", presets=" + std::to_string(pc->second);
            auto nt = r.notes.find(p.id);
            if (nt != r.notes.end())
                line += ", note=" + nt->second;
            auto sm = r.samples.find(p.id);
            if (sm != r.samples.end() && !sm->second.empty())
            {
                line += ", samples=[";
                for (size_t i = 0; i < sm->second.size(); ++i)
                    line += (i > 0 ? "; " : "") + sm->second[i].first
                            + " | " + sm->second[i].second;
                line += "]";
            }
        }
        std::cout << line << "\n";
        logBoth(line);
    }
    std::cout << "==== end summary (" << modulesTested << " module(s) loaded of "
              << files.size() << " file(s)) ====\n" << std::endl;

    EXPECT_GE(modulesTested, 1);
}
