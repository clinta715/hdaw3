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