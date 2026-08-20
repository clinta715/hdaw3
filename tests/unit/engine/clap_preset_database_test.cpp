#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/CLAPPresetDatabase.h"
#include "common/DebugLog.h"

#include <clap/factory/preset-discovery.h>

// Hermetic Gate-2 suite for CLAPPresetDatabase: fake clap factories as plain
// C structs (shapes mirror clap_preset_probe_test.cpp). No real plugins.

namespace {

struct FakeProviderConfig
{
    std::vector<std::pair<uint32_t, std::string>> locations; // (kind, location)
    std::vector<std::string> extensions;                     // empty = match all
    std::function<void(uint32_t kind, const char* location,
                       const clap_preset_discovery_metadata_receiver_t*)> emit;
    std::atomic<int> initCount{ 0 };
    std::vector<std::string> queriedPaths; // FILE-kind get_metadata arguments
    const clap_preset_discovery_indexer_t* indexer = nullptr;
};

FakeProviderConfig* g_cfg = nullptr;

void emitPreset(const clap_preset_discovery_metadata_receiver_t* r,
                const char* name, const char* loadKey,
                const std::vector<std::pair<const char*, const char*>>& ids)
{
    r->begin_preset(r, name, loadKey);
    for (const auto& id : ids)
    {
        clap_universal_plugin_id_t pid{};
        pid.abi = id.first;
        pid.id = id.second;
        r->add_plugin_id(r, &pid);
    }
}

bool CLAP_ABI fakeProviderInit(const clap_preset_discovery_provider_t* p)
{
    auto* cfg = static_cast<FakeProviderConfig*>(p->provider_data);
    cfg->initCount.fetch_add(1);
    const auto* ix = cfg->indexer;
    if (ix == nullptr)
        return false;
    for (const auto& ext : cfg->extensions)
    {
        clap_preset_discovery_filetype_t ft{};
        ft.name = "fake";
        ft.description = nullptr;
        ft.file_extension = ext.empty() ? nullptr : ext.c_str();
        ix->declare_filetype(ix, &ft);
    }
    for (const auto& loc : cfg->locations)
    {
        clap_preset_discovery_location_t l{};
        l.flags = 0;
        l.name = "fake-location";
        l.kind = loc.first;
        l.location = loc.second.empty() ? nullptr : loc.second.c_str();
        ix->declare_location(ix, &l);
    }
    return true;
}

void CLAP_ABI fakeProviderDestroy(const clap_preset_discovery_provider_t*) {}

bool CLAP_ABI fakeProviderGetMetadata(const clap_preset_discovery_provider_t* p,
                                      uint32_t location_kind, const char* location,
                                      const clap_preset_discovery_metadata_receiver_t* receiver)
{
    auto* cfg = static_cast<FakeProviderConfig*>(p->provider_data);
    if (location_kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location != nullptr)
        cfg->queriedPaths.push_back(location);
    if (cfg->emit)
        cfg->emit(location_kind, location, receiver);
    return true;
}

const void* CLAP_ABI fakeProviderGetExtension(const clap_preset_discovery_provider_t*,
                                              const char*)
{
    return nullptr;
}

clap_preset_discovery_provider_descriptor_t g_providerDesc = {
    CLAP_VERSION_INIT, "fake.provider", "Fake Provider", "HDAW"
};

uint32_t CLAP_ABI fakeFactoryCount(const clap_preset_discovery_factory_t*)
{
    return 1;
}

const clap_preset_discovery_provider_descriptor_t* CLAP_ABI fakeFactoryGetDescriptor(
    const clap_preset_discovery_factory_t*, uint32_t)
{
    return &g_providerDesc;
}

const clap_preset_discovery_provider_t* CLAP_ABI fakeFactoryCreate(
    const clap_preset_discovery_factory_t*,
    const clap_preset_discovery_indexer_t* indexer,
    const char*)
{
    if (g_cfg == nullptr)
        return nullptr;
    g_cfg->indexer = indexer;
    static clap_preset_discovery_provider_t provider{};
    provider.desc = &g_providerDesc;
    provider.provider_data = g_cfg;
    provider.init = &fakeProviderInit;
    provider.destroy = &fakeProviderDestroy;
    provider.get_metadata = &fakeProviderGetMetadata;
    provider.get_extension = &fakeProviderGetExtension;
    return &provider;
}

clap_preset_discovery_factory_t g_fakeFactory = {
    &fakeFactoryCount, &fakeFactoryGetDescriptor, &fakeFactoryCreate
};

juce::File makeTempDir(const juce::String& suffix)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_clap_db_test_" + suffix + "_"
                      + juce::String(juce::Random::getSystemRandom().nextInt(1000000)));
    EXPECT_TRUE(dir.createDirectory());
    return dir;
}

} // namespace

TEST(ClapPresetDatabase, PluginKindFiltersByUniversalId)
{
    FakeProviderConfig cfg;
    cfg.locations = { { CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "" } };
    cfg.emit = [](uint32_t, const char*,
                  const clap_preset_discovery_metadata_receiver_t* r) {
        emitPreset(r, "P1", "k1", { { "clap", "com.test.synth" } });
        emitPreset(r, "P2", "k2", { { "clap", "com.test.synth" } });
        emitPreset(r, "P3", "k3", { { "clap", "com.other.plugin" } });
    };
    g_cfg = &cfg;

    auto presets = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://pluginkind", &g_fakeFactory, { "com.test.synth" }, 2000);
    ASSERT_NE(presets, nullptr);
    presets->ensureBuilt();
    ASSERT_TRUE(presets->isReady());

    const auto* list = presets->presetsFor("com.test.synth");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 2u);
    EXPECT_EQ((*list)[0].name, "P1");
    EXPECT_EQ((*list)[0].loadKey, "k1");
    EXPECT_EQ((*list)[0].locationKind, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    EXPECT_TRUE((*list)[0].location.isEmpty());
    EXPECT_EQ((*list)[1].name, "P2");
    EXPECT_EQ((*list)[1].loadKey, "k2");

    // The preset declared for another plugin id must not leak in.
    EXPECT_EQ(presets->presetsFor("com.other.plugin"), nullptr);
    g_cfg = nullptr;
}

TEST(ClapPresetDatabase, FileKindCrawlsDeclaredExtensionsOnly)
{
    auto dir = makeTempDir("filekind");
    dir.getChildFile("a.synthpatch").replaceWithText("patch-a");
    dir.getChildFile("b.synthpatch").replaceWithText("patch-b");
    dir.getChildFile("c.txt").replaceWithText("not-a-patch");

    FakeProviderConfig cfg;
    cfg.locations = { { CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                        dir.getFullPathName().toStdString() } };
    cfg.extensions = { "synthpatch" };
    cfg.emit = [](uint32_t, const char* location,
                  const clap_preset_discovery_metadata_receiver_t* r) {
        const std::string name = juce::File(juce::String::fromUTF8(location))
            .getFileName().toStdString();
        emitPreset(r, name.c_str(), "", { { "clap", "com.test.synth" } });
    };
    g_cfg = &cfg;

    auto presets = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://filekind", &g_fakeFactory, { "com.test.synth" }, 2000);
    presets->ensureBuilt();

    const auto* list = presets->presetsFor("com.test.synth");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 2u);
    for (const auto& e : *list)
    {
        EXPECT_EQ(e.locationKind, CLAP_PRESET_DISCOVERY_LOCATION_FILE);
        EXPECT_TRUE(e.location.endsWith("synthpatch"));
        EXPECT_TRUE(e.loadKey.isEmpty());
    }

    bool sawTxt = false;
    for (const auto& q : cfg.queriedPaths)
        if (juce::String::fromUTF8(q.c_str()).endsWith(".txt"))
            sawTxt = true;
    EXPECT_FALSE(sawTxt) << ".txt file must never be sent to get_metadata";
    EXPECT_EQ(cfg.queriedPaths.size(), 2u);

    g_cfg = nullptr;
    dir.deleteRecursively();
}

TEST(ClapPresetDatabase, FileCapLimitsCrawl)
{
    auto dir = makeTempDir("cap");
    for (int i = 0; i < 6; ++i)
        dir.getChildFile("p" + juce::String(i) + ".synthpatch")
           .replaceWithText("patch");

    FakeProviderConfig cfg;
    cfg.locations = { { CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                        dir.getFullPathName().toStdString() } };
    cfg.extensions = { "synthpatch" };
    cfg.emit = [](uint32_t, const char* location,
                  const clap_preset_discovery_metadata_receiver_t* r) {
        emitPreset(r, location, "", { { "clap", "com.test.synth" } });
    };
    g_cfg = &cfg;

    auto presets = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://cap", &g_fakeFactory, { "com.test.synth" }, 4);
    presets->ensureBuilt();

    const auto* list = presets->presetsFor("com.test.synth");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 4u);
    EXPECT_EQ(cfg.queriedPaths.size(), 4u);

    g_cfg = nullptr;
    dir.deleteRecursively();
}

TEST(ClapPresetDatabase, NoPluginIdCountsForAllModulePlugins)
{
    FakeProviderConfig cfg;
    cfg.locations = { { CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "" } };
    cfg.emit = [](uint32_t, const char*,
                  const clap_preset_discovery_metadata_receiver_t* r) {
        emitPreset(r, "Shared", "sk", {}); // no plugin ids at all
    };
    g_cfg = &cfg;

    auto presets = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://noid", &g_fakeFactory, { "com.test.a", "com.test.b" }, 2000);
    presets->ensureBuilt();

    const auto* listA = presets->presetsFor("com.test.a");
    const auto* listB = presets->presetsFor("com.test.b");
    ASSERT_NE(listA, nullptr);
    ASSERT_NE(listB, nullptr);
    ASSERT_EQ(listA->size(), 1u);
    ASSERT_EQ(listB->size(), 1u);
    EXPECT_EQ((*listA)[0].name, "Shared");
    EXPECT_EQ((*listB)[0].name, "Shared");
    g_cfg = nullptr;
}

TEST(ClapPresetDatabase, CacheOncePerModulePath)
{
    FakeProviderConfig cfg;
    cfg.locations = { { CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "" } };
    cfg.emit = [](uint32_t, const char*,
                  const clap_preset_discovery_metadata_receiver_t* r) {
        emitPreset(r, "X", "", { { "clap", "com.test.synth" } });
    };
    g_cfg = &cfg;

    auto first = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://cacheonce", &g_fakeFactory, { "com.test.synth" }, 2000);
    auto second = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://cacheonce", &g_fakeFactory, { "com.test.synth" }, 2000);
    EXPECT_EQ(first.get(), second.get());

    first->ensureBuilt();
    second->ensureBuilt();
    EXPECT_EQ(cfg.initCount.load(), 1) << "the provider must be built exactly once";
    g_cfg = nullptr;
}

TEST(ClapPresetDatabase, NoFactoryYieldsEmpty)
{
    auto presets = CLAPPresetDatabase::ModulePresets::getForModulePath(
        "test://nofactory", nullptr, { "com.test.synth" }, 2000);
    ASSERT_NE(presets, nullptr);
    presets->ensureBuilt();
    EXPECT_TRUE(presets->isReady());
    EXPECT_EQ(presets->presetsFor("com.test.synth"), nullptr);
}
