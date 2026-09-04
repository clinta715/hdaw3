#include <gtest/gtest.h>
#include "engine/PluginManager.h"
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Verifies PluginManager::resolveIdentifierToPath, the logic that lets the
// non-isolated (in-process) render path load plugins by their identifier
// string. Track::rebuildFXChain passes a descriptor whose fileOrIdentifier is
// the plugin *identifier* (e.g. "CLAP-Vital-aaca468a-0"), but JUCE's
// AudioPluginFormatManager only matches a format when fileOrIdentifier ends
// with that format's extension (e.g. ".clap"). The helper resolves the
// identifier back to the real plugin file path recorded during the scan.

namespace
{
void addToKnown(juce::KnownPluginList& known, const juce::PluginDescription& real)
{
    known.addType(real);
}
}

TEST(PluginIdentifierResolution, ResolvesIdentifierToRealPath)
{
    // A descriptor as recorded by a successful scan: fileOrIdentifier is the
    // real plugin file path.
    juce::PluginDescription real;
    real.name = "Vital";
    real.manufacturerName = "Matt Tytel";
    real.pluginFormatName = "CLAP";
    real.fileOrIdentifier = "C:\\Program Files\\Common Files\\CLAP\\Vital.clap";
    real.uniqueId = 0xaaca468a;
    real.isInstrument = true;

    auto known = juce::KnownPluginList();
    addToKnown(known, real);

    // A descriptor as built by Track::rebuildFXChain: fileOrIdentifier is the
    // identifier string, name is left empty.
    juce::PluginDescription id;
    id.pluginFormatName = "CLAP";
    id.fileOrIdentifier = real.createIdentifierString();
    id.name.clear();

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(id, known);

    EXPECT_EQ(resolved.fileOrIdentifier, real.fileOrIdentifier);
    EXPECT_EQ(resolved.name, "Vital");
    EXPECT_EQ(resolved.pluginFormatName, "CLAP");
}

TEST(PluginIdentifierResolution, AlreadyAPathIsLeftUnchanged)
{
    juce::PluginDescription real;
    real.name = "Vital";
    real.pluginFormatName = "CLAP";
    real.fileOrIdentifier = "C:\\Program Files\\Common Files\\CLAP\\Vital.clap";

    auto known = juce::KnownPluginList();
    addToKnown(known, real);

    // A desc whose fileOrIdentifier already ends in ".clap" must not be altered.
    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(real, known);
    EXPECT_EQ(resolved.fileOrIdentifier, real.fileOrIdentifier);
    EXPECT_EQ(resolved.name, "Vital");
}

TEST(PluginIdentifierResolution, Vst3PathIsLeftUnchanged)
{
    juce::PluginDescription vst;
    vst.name = "SomeVst";
    vst.pluginFormatName = "VST3";
    vst.fileOrIdentifier = "C:\\Program Files\\Common Files\\VST3\\SomeVst.vst3";

    auto known = juce::KnownPluginList();
    addToKnown(known, vst);

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(vst, known);
    EXPECT_EQ(resolved.fileOrIdentifier, vst.fileOrIdentifier);
}

TEST(PluginIdentifierResolution, UnknownIdentifierReturnsDescUnchanged)
{
    juce::PluginDescription real;
    real.name = "Vital";
    real.pluginFormatName = "CLAP";
    real.fileOrIdentifier = "C:\\Program Files\\Common Files\\CLAP\\Vital.clap";

    auto known = juce::KnownPluginList();
    addToKnown(known, real);

    juce::PluginDescription id;
    id.pluginFormatName = "CLAP";
    id.fileOrIdentifier = "CLAP-NoSuchPlugin-00000000-0";

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(id, known);

    // No match -> unchanged, and no crash.
    EXPECT_EQ(resolved.fileOrIdentifier, id.fileOrIdentifier);
    EXPECT_TRUE(resolved.name.isEmpty());
}

TEST(PluginIdentifierResolution, EmptyKnownListDoesNotCrash)
{
    juce::KnownPluginList known;

    juce::PluginDescription id;
    id.pluginFormatName = "CLAP";
    id.fileOrIdentifier = "CLAP-Vital-aaca468a-0";

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(id, known);
    EXPECT_EQ(resolved.fileOrIdentifier, id.fileOrIdentifier);
}

TEST(PluginIdentifierResolution, PreservesUniqueIdFromKnownEntry)
{
    juce::PluginDescription real;
    real.name = "SomeVst";
    real.pluginFormatName = "VST3";
    real.fileOrIdentifier = "C:\\Program Files\\Common Files\\VST3\\SomeVst.vst3";
    real.uniqueId = 0x3d9dac4c;
    real.deprecatedUid = 0x11223344;

    auto known = juce::KnownPluginList();
    addToKnown(known, real);

    juce::PluginDescription id;
    id.pluginFormatName = "VST3";
    id.fileOrIdentifier = real.createIdentifierString();
    id.name.clear();

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(id, known);

    EXPECT_EQ(resolved.fileOrIdentifier, real.fileOrIdentifier);
    EXPECT_EQ(resolved.name, "SomeVst");
    EXPECT_EQ(resolved.uniqueId, real.uniqueId);
    EXPECT_EQ(resolved.deprecatedUid, real.deprecatedUid);
}

TEST(PluginIdentifierResolution, FallbackMatchPreservesUniqueId)
{
    juce::PluginDescription real;
    real.name = "Vital";
    real.pluginFormatName = "CLAP";
    real.fileOrIdentifier = "C:\\Program Files\\Common Files\\CLAP\\Vital.clap";
    real.uniqueId = 0xaaca468a;

    auto known = juce::KnownPluginList();
    addToKnown(known, real);

    // Identifier hash matches nothing, so resolution must fall back to the
    // format+name match.
    juce::PluginDescription id;
    id.name = "Vital";
    id.pluginFormatName = "CLAP";
    id.fileOrIdentifier = "CLAP-NoSuchHash-ffffffff-0";

    auto resolved = HDAW::PluginManager::resolveIdentifierToPath(id, known);

    EXPECT_EQ(resolved.fileOrIdentifier, real.fileOrIdentifier);
    EXPECT_EQ(resolved.uniqueId, real.uniqueId);
}

TEST(PluginManagerInProcessVst3, InstantiatesRealVst3ByIdentifier)
{
    HDAW::PluginManager pm;
    pm.isolationEnabled = false;

    const juce::PluginDescription* vst3 = nullptr;
    for (const auto& d : pm.getPlugins())
    {
        if (d.pluginFormatName == "VST3")
        {
            vst3 = &d;
            break;
        }
    }
    if (vst3 == nullptr)
        GTEST_SKIP() << "No VST3 plugins in cache";
    // Stale scan cache: entry may point to a file that no longer exists
    if (!juce::File(vst3->fileOrIdentifier).existsAsFile())
    {
        bool anyValid = false;
        for (const auto& d : pm.getPlugins())
            if (d.pluginFormatName == "VST3" && juce::File(d.fileOrIdentifier).existsAsFile())
                anyValid = true;
        if (!anyValid)
            GTEST_SKIP() << "No VST3 plugin file exists on disk (stale scan cache)";
    }

    juce::PluginDescription desc;
    desc.fileOrIdentifier = vst3->createIdentifierString();
    desc.pluginFormatName = "VST3";

    juce::String error;
    auto instance = pm.createPluginInstance(desc, error, 44100.0, 512, false);
    EXPECT_NE(instance, nullptr) << error;
}