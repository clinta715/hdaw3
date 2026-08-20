#include "CLAPPresetDatabase.h"
#include "CLAPPluginFormat.h"
#include "../common/DebugLog.h"
#include <algorithm>
#include <cstring>

// Enumeration mirrors the proven Phase-0 probe
// (tests/unit/engine/clap_preset_probe_test.cpp): indexer records declared
// locations/filetypes; PLUGIN-kind locations are queried directly; FILE-kind
// locations are crawled for declared extensions with a per-location file cap;
// the receiver buckets presets by universal plugin id.

namespace {

const char* kTag = "clap_preset_db";

struct IndexerState
{
    std::vector<juce::String> extensions; // empty entry = match all files
    std::vector<std::pair<uint32_t, juce::String>> locations; // (kind, location)
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
    st->extensions.push_back(
        (ft->file_extension != nullptr && ft->file_extension[0] != '\0')
            ? juce::String::fromUTF8(ft->file_extension) : juce::String());
    return true;
}

bool CLAP_ABI ixDeclareLocation(const clap_preset_discovery_indexer_t* ix,
                                const clap_preset_discovery_location_t* loc)
{
    if (loc == nullptr)
        return false;
    auto* st = indexerStateOf(ix);
    st->locations.emplace_back(loc->kind,
        loc->location != nullptr ? juce::String::fromUTF8(loc->location)
                                 : juce::String());
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

struct PresetRecord
{
    juce::String name;
    juce::String loadKey;
    std::vector<std::pair<juce::String, juce::String>> pluginIds; // (abi, id)
};

struct ReceiverState
{
    const std::vector<juce::String>* moduleIds = nullptr;
    std::map<juce::String, std::vector<CLAPPresetEntry>>* out = nullptr;
    uint32_t currentKind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
    juce::String currentLocation;

    std::vector<PresetRecord> records;
    bool hasPending = false;
    PresetRecord pending;

    void commitPending()
    {
        if (hasPending)
        {
            records.push_back(std::move(pending));
            pending = {};
            hasPending = false;
        }
    }

    // Bucket committed records by module plugin id and clear them. Presets
    // declaring NO plugin id count for every plugin of the module.
    void bucketize()
    {
        commitPending();
        for (const auto& rec : records)
        {
            std::vector<juce::String> matched;
            if (rec.pluginIds.empty())
            {
                matched = *moduleIds;
            }
            else
            {
                for (const auto& pid : rec.pluginIds)
                {
                    if (pid.first != "clap")
                        continue;
                    for (const auto& moduleId : *moduleIds)
                        if (pid.second == moduleId
                            && std::find(matched.begin(), matched.end(), moduleId) == matched.end())
                            matched.push_back(moduleId);
                }
            }

            for (const auto& moduleId : matched)
            {
                CLAPPresetEntry entry;
                entry.name = rec.name;
                entry.loadKey = rec.loadKey;
                entry.location = currentLocation;
                entry.locationKind = currentKind;
                (*out)[moduleId].push_back(std::move(entry));
            }
        }
        records.clear();
    }
};

ReceiverState* receiverStateOf(const clap_preset_discovery_metadata_receiver_t* r)
{
    return static_cast<ReceiverState*>(r->receiver_data);
}

void CLAP_ABI recvOnError(const clap_preset_discovery_metadata_receiver_t* r,
                          int32_t osError, const char* msg)
{
    juce::ignoreUnused(r);
    HDAW_LOG(kTag, juce::String("receiver on_error: ")
        + (msg != nullptr ? msg : "") + " os=" + juce::String(osError));
}

bool CLAP_ABI recvBeginPreset(const clap_preset_discovery_metadata_receiver_t* r,
                              const char* name, const char* loadKey)
{
    auto* st = receiverStateOf(r);
    st->commitPending();
    st->hasPending = true;
    st->pending.name = name != nullptr ? juce::String::fromUTF8(name) : juce::String();
    st->pending.loadKey = loadKey != nullptr ? juce::String::fromUTF8(loadKey) : juce::String();
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
    st->pending.pluginIds.emplace_back(
        pid->abi != nullptr ? juce::String::fromUTF8(pid->abi) : juce::String(),
        pid->id != nullptr ? juce::String::fromUTF8(pid->id) : juce::String());
}

void CLAP_ABI recvNoop(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void CLAP_ABI recvSetFlags(const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
void CLAP_ABI recvSetTimestamps(const clap_preset_discovery_metadata_receiver_t*,
                                clap_timestamp, clap_timestamp) {}
void CLAP_ABI recvAddExtraInfo(const clap_preset_discovery_metadata_receiver_t*,
                               const char*, const char*) {}

} // namespace

// ── ModulePresets ───────────────────────────────────────────────

CLAPPresetDatabase::ModulePresets::ModulePresets(
    const clap_preset_discovery_factory_t* f,
    std::vector<juce::String> ids,
    int cap)
    : factory(f), modulePluginIds(std::move(ids)),
      fileCap(cap > 0 ? cap : kDefaultFileCap)
{
}

CLAPPresetDatabase::ModulePresets::~ModulePresets()
{
    if (asyncThread != nullptr && asyncThread->joinable())
        asyncThread->join();
}

void CLAPPresetDatabase::ModulePresets::ensureBuilt()
{
    std::call_once(buildOnce, [this] { build(); });
}

void CLAPPresetDatabase::ModulePresets::startBuildAsync()
{
    bool expected = false;
    if (!asyncStarted.compare_exchange_strong(expected, true))
        return;
    if (ready.load(std::memory_order_acquire))
        return; // built synchronously elsewhere (tests)
    asyncThread = std::make_unique<std::thread>([this] { ensureBuilt(); });
}

void CLAPPresetDatabase::ModulePresets::setModuleKeepalive(std::shared_ptr<CLAPModule> module)
{
    std::lock_guard<std::mutex> lock(keepaliveMutex);
    if (moduleKeepalive == nullptr && !ready.load(std::memory_order_acquire))
        moduleKeepalive = std::move(module);
}

const std::vector<CLAPPresetEntry>*
CLAPPresetDatabase::ModulePresets::presetsFor(const juce::String& clapPluginId) const
{
    if (!ready.load(std::memory_order_acquire))
        return nullptr;
    auto it = byPlugin.find(clapPluginId);
    if (it == byPlugin.end() || it->second.empty())
        return nullptr;
    return &it->second;
}

void CLAPPresetDatabase::ModulePresets::build()
{
    if (factory == nullptr)
    {
        HDAW_LOG(kTag, "no preset-discovery factory; module exposes no presets");
        ready.store(true, std::memory_order_release);
        return;
    }

    try
    {
    ReceiverState st;
    st.moduleIds = &modulePluginIds;
    st.out = &byPlugin;

    clap_preset_discovery_metadata_receiver_t receiver{};
    receiver.receiver_data = &st;
    receiver.on_error = &recvOnError;
    receiver.begin_preset = &recvBeginPreset;
    receiver.add_plugin_id = &recvAddPluginId;
    receiver.set_soundpack_id = &recvNoop;
    receiver.set_flags = &recvSetFlags;
    receiver.add_creator = &recvNoop;
    receiver.set_description = &recvNoop;
    receiver.set_timestamps = &recvSetTimestamps;
    receiver.add_feature = &recvNoop;
    receiver.add_extra_info = &recvAddExtraInfo;

    uint32_t providerCount = factory->count(factory);

    for (uint32_t pi = 0; pi < providerCount; ++pi)
    {
        const auto* pdesc = factory->get_descriptor(factory, pi);
        if (pdesc == nullptr || pdesc->id == nullptr)
            continue;
        const juce::String providerId = juce::String::fromUTF8(pdesc->id);

        IndexerState ixState;
        clap_preset_discovery_indexer_t indexer{};
        indexer.clap_version = CLAP_VERSION_INIT;
        indexer.name = "HDAW";
        indexer.vendor = "HDAW";
        indexer.url = "";
        indexer.version = "0.23.1";
        indexer.indexer_data = &ixState;
        indexer.declare_filetype = &ixDeclareFiletype;
        indexer.declare_location = &ixDeclareLocation;
        indexer.declare_soundpack = &ixDeclareSoundpack;
        indexer.get_extension = &ixGetExtension;

        const auto* provider = factory->create(factory, &indexer, providerId.toRawUTF8());
        if (provider == nullptr)
        {
            HDAW_LOG(kTag, "provider create failed: " + providerId);
            continue;
        }
        // destroy() must run even when a get_metadata call throws.
        struct ProviderGuard
        {
            const clap_preset_discovery_provider_t* p;
            ~ProviderGuard() { if (p != nullptr) p->destroy(p); }
        } providerGuard{ provider };

        if (!provider->init(provider))
        {
            HDAW_LOG(kTag, "provider init failed: " + providerId);
            continue;
        }

        for (const auto& loc : ixState.locations)
        {
            if (loc.first == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
            {
                // Presets bundled in the DSO; location must be null.
                st.currentKind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN;
                st.currentLocation = {};
                provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                       nullptr, &receiver);
                st.bucketize();
            }
            else if (loc.first == CLAP_PRESET_DISCOVERY_LOCATION_FILE)
            {
                st.currentKind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
                juce::File locFile(loc.second);
                if (locFile.existsAsFile())
                {
                    st.currentLocation = locFile.getFullPathName();
                    provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                           st.currentLocation.toRawUTF8(), &receiver);
                    st.bucketize();
                    continue;
                }
                if (!locFile.isDirectory())
                {
                    HDAW_LOG(kTag, "FILE location does not exist: " + loc.second);
                    continue;
                }

                juce::StringArray patterns;
                for (const auto& ext : ixState.extensions)
                    patterns.add(ext.isNotEmpty() ? ("*." + ext) : juce::String("*"));
                if (patterns.isEmpty())
                    patterns.add("*"); // no filetype declared -> take all files

                int filesSent = 0;
                bool capHit = false;
                for (auto entry : juce::RangedDirectoryIterator(
                         locFile, true, patterns.joinIntoString(";"), juce::File::findFiles))
                {
                    if (filesSent >= fileCap)
                    {
                        capHit = true;
                        break;
                    }
                    st.currentLocation = entry.getFile().getFullPathName();
                    provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                           st.currentLocation.toRawUTF8(), &receiver);
                    st.bucketize();
                    ++filesSent;
                }
                if (capHit)
                    HDAW_LOG(kTag, "file cap (" + juce::String(fileCap)
                        + ") hit at location: " + loc.second);
            }
        }
    }
    }
    catch (const std::exception& e)
    {
        HDAW_LOG(kTag, juce::String("enumeration failed: ") + e.what());
    }
    catch (...)
    {
        HDAW_LOG(kTag, "enumeration failed (unknown exception)");
    }

    // Entries are copied out; the DSO no longer needs to be pinned.
    {
        std::lock_guard<std::mutex> lock(keepaliveMutex);
        moduleKeepalive.reset();
    }
    ready.store(true, std::memory_order_release);
}

// ── Registry ────────────────────────────────────────────────────

std::mutex CLAPPresetDatabase::registryMutex;

std::map<juce::String, std::shared_ptr<CLAPPresetDatabase::ModulePresets>>&
CLAPPresetDatabase::registry()
{
    static std::map<juce::String, std::shared_ptr<ModulePresets>> r;
    return r;
}

std::shared_ptr<CLAPPresetDatabase::ModulePresets>
CLAPPresetDatabase::ModulePresets::getForModulePath(
    const juce::String& modulePath,
    const clap_preset_discovery_factory_t* factory,
    const std::vector<juce::String>& modulePluginIds,
    int fileCap)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    auto& reg = registry();
    auto it = reg.find(modulePath);
    if (it != reg.end())
        return it->second;
    auto created = std::make_shared<ModulePresets>(factory, modulePluginIds, fileCap);
    reg.emplace(modulePath, created);
    return created;
}

std::shared_ptr<CLAPPresetDatabase::ModulePresets>
CLAPPresetDatabase::ModulePresets::getForModule(
    const juce::String& modulePath,
    const std::shared_ptr<CLAPModule>& module)
{
    if (module == nullptr || module->entry == nullptr)
        return nullptr;

    const auto* factory = static_cast<const clap_preset_discovery_factory_t*>(
        module->entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (factory == nullptr)
        factory = static_cast<const clap_preset_discovery_factory_t*>(
            module->entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));

    std::vector<juce::String> ids;
    if (module->factory != nullptr)
    {
        const uint32_t count = module->factory->get_plugin_count(module->factory);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* d = module->factory->get_plugin_descriptor(module->factory, i);
            if (d != nullptr && d->id != nullptr)
                ids.push_back(juce::String::fromUTF8(d->id));
        }
    }

    auto presets = getForModulePath(modulePath, factory, ids, kDefaultFileCap);
    // The build (possibly async) dereferences entry/factory — keep the DSO
    // loaded until it ran (first writer wins; released right after the build).
    if (presets != nullptr)
        presets->setModuleKeepalive(module);
    return presets;
}
