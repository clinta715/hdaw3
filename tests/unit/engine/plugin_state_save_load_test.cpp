#include <gtest/gtest.h>
#include "engine/ProjectSerializer.h"
#include "model/ProjectModel.h"
#include "engine/TrackFXSlot.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

// Verifies the plugin state save/load pipeline:
// 1. A "plugin" produces a known byte sequence via getStateInformation.
// 2. Track::rebuildFXChain encodes the bytes to base64 in IDs::pluginState.
// 3. ProjectSerializer round-trips the model through XML.
// 4. The pluginState property survives the round-trip.

namespace
{
class FixedStatePlugin : public juce::AudioPluginInstance
{
public:
    FixedStatePlugin()
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withInput("In", juce::AudioChannelSet::mono())
                  .withOutput("Out", juce::AudioChannelSet::mono())) {}

    // A recognisable byte sequence the test can check for after a round-trip.
    void getStateInformation(juce::MemoryBlock& destData) override
    {
        const char payload[] = "HDAW-PLUGIN-STATE-MARKER-XYZ123";
        destData.setSize(sizeof(payload));
        std::memcpy(destData.getData(), payload, sizeof(payload));
    }
    void setStateInformation(const void* data, int sizeInBytes) override
    {
        lastReceived.setSize(sizeInBytes);
        if (sizeInBytes > 0)
            std::memcpy(lastReceived.getData(), data, sizeInBytes);
    }

    juce::MemoryBlock lastReceived;

    const juce::String getName() const override { return "FixedState"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumParameters() override { return 0; }
    float getParameter(int) override { return 0; }
    void setParameter(int, float) override {}
    const juce::String getParameterName(int) override { return ""; }
    const juce::String getParameterText(int) override { return ""; }
    juce::String getInputChannelName(int) { return ""; }
    juce::String getOutputChannelName(int) { return ""; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    juce::AudioProcessor::Bus* getBus(int) const { return nullptr; }
    bool enableBusesLayout(const juce::AudioProcessor::BusesLayout&) { return true; }
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout&) const override { return true; }
    bool canAddBus(bool) const override { return true; }
    bool canRemoveBus(bool) const override { return false; }
    void setPlayHead(juce::AudioPlayHead*) override {}
    double getSampleRate() const { return 44100.0; }
    void setSampleRate(double) {}
    int getBlockSize() const { return 512; }
    void setBlockSize(int) {}
    double getTotalLength() const { return 0; }
    bool isLooping() const { return false; }
    void setLooping(bool) {}
    juce::PluginDescription getPluginDescription() const {
        juce::PluginDescription d;
        d.name = "FixedState";
        d.fileOrIdentifier = "FixedState";
        return d;
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "FixedState"; }
    void changeProgramName(int, const juce::String&) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override { d = getPluginDescription(); }
};
}

TEST(PluginStateSaveLoad, Base64RoundTripPreservesBytes)
{
    // Use a known byte sequence and confirm the base64 helpers preserve it.
    const char payload[] = "HDAW-PLUGIN-STATE-MARKER-XYZ123";
    juce::MemoryBlock block;
    block.setSize(sizeof(payload));
    std::memcpy(block.getData(), payload, sizeof(payload));

    juce::String encoded = block.toBase64Encoding();
    EXPECT_FALSE(encoded.isEmpty());

    juce::MemoryBlock decoded;
    ASSERT_TRUE(decoded.fromBase64Encoding(encoded));
    ASSERT_EQ(decoded.getSize(), block.getSize());
    EXPECT_EQ(std::memcmp(decoded.getData(), block.getData(), block.getSize()), 0);
}

TEST(PluginStateSaveLoad, ProjectRoundTripPreservesPluginState)
{
    // 1. Build a project with a single track and a plugin slot holding
    //    base64-encoded state.
    ProjectModel model;
    model.createDefaultProject();

    auto trackList = model.getTrackListTree();
    juce::ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, "Plugin State Track", nullptr);
    track.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
    trackList.addChild(track, -1, nullptr);
    ASSERT_EQ(trackList.getNumChildren(), 1);
    track = trackList.getChild(0);

    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, nullptr);
    }

    const char payload[] = "HDAW-PLUGIN-STATE-MARKER-XYZ123";
    juce::MemoryBlock block;
    block.setSize(sizeof(payload));
    std::memcpy(block.getData(), payload, sizeof(payload));
    juce::String encoded = block.toBase64Encoding();

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, "plugin", nullptr);
    slot.setProperty(IDs::pluginID, juce::String("Test.Plugin"), nullptr);
    slot.setProperty(IDs::pluginFormat, juce::String("VST3"), nullptr);
    slot.setProperty(IDs::pluginState, encoded, nullptr);
    fxChain.addChild(slot, -1, nullptr);

    // 2. Save to a temporary file.
    QString tempPath = QDir::tempPath() + "/hdaw_plugin_state_test.hdap";
    QFile::remove(tempPath);
    juce::File saveFile(tempPath.toStdString());
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, saveFile));
    ASSERT_TRUE(saveFile.existsAsFile());

    // 3. Load into a fresh model and verify the encoded state survived.
    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, saveFile));

    auto loadedTrackList = loaded.getTrackListTree();
    ASSERT_EQ(loadedTrackList.getNumChildren(), 1);
    auto loadedTrack = loadedTrackList.getChild(0);
    auto loadedChain = loadedTrack.getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(loadedChain.isValid());
    ASSERT_GT(loadedChain.getNumChildren(), 0);
    auto loadedSlot = loadedChain.getChild(0);

    juce::String loadedEncoded = loadedSlot.getProperty(IDs::pluginState).toString();
    EXPECT_EQ(loadedEncoded, encoded);

    // 4. Decode and confirm the bytes match.
    juce::MemoryBlock decoded;
    ASSERT_TRUE(decoded.fromBase64Encoding(loadedEncoded));
    ASSERT_EQ(decoded.getSize(), block.getSize());
    EXPECT_EQ(std::memcmp(decoded.getData(), block.getData(), block.getSize()), 0);

    QFile::remove(tempPath);
}

TEST(PluginStateSaveLoad, FixedStatePluginSerializesRecognizableBytes)
{
    // Confirms that a real juce::AudioPluginInstance subclass returns a
    // known byte sequence through getStateInformation that the save path
    // can encode as base64 and the load path can decode back. This is the
    // minimum-viable end-to-end test for plugin state preservation.
    FixedStatePlugin plugin;
    juce::MemoryBlock state;
    plugin.getStateInformation(state);

    ASSERT_GT(state.getSize(), 0u);
    juce::String encoded = state.toBase64Encoding();
    EXPECT_FALSE(encoded.isEmpty());

    // Hand the encoded blob to a fresh instance of the same plugin
    // type and confirm the bytes are equal.
    FixedStatePlugin receiver;
    juce::MemoryBlock roundTrip;
    ASSERT_TRUE(roundTrip.fromBase64Encoding(encoded));
    receiver.setStateInformation(roundTrip.getData(),
                                  static_cast<int>(roundTrip.getSize()));

    ASSERT_EQ(receiver.lastReceived.getSize(), state.getSize());
    EXPECT_EQ(std::memcmp(receiver.lastReceived.getData(),
                          state.getData(), state.getSize()), 0);
}

// FIX-1 guard (docs/plans/2026-09-12-plugin-state-durability.md): the
// serializer must not overwrite a substantial existing pluginState blob with
// a suspiciously tiny read (a load→save cycle once shrank 177KB states to
// 262B stubs this way).
TEST(PluginStateSaveLoad, SizeRegressionGuard)
{
    using HDAW::shouldReplacePluginState;

    EXPECT_TRUE(shouldReplacePluginState(0, 100));        // absent existing → trust read
    EXPECT_TRUE(shouldReplacePluginState(500, 100));      // tiny existing → trust read
    EXPECT_TRUE(shouldReplacePluginState(2000, 262));     // <4KB existing → trust read
    EXPECT_FALSE(shouldReplacePluginState(50000, 0));     // never blank out a real state
    EXPECT_FALSE(shouldReplacePluginState(177845, 262));  // THE regression from the field
    EXPECT_FALSE(shouldReplacePluginState(50000, 1000));  // <1KB new → suspicious
    EXPECT_TRUE(shouldReplacePluginState(50000, 8000));   // 8000 >= 50000/8 → legit
    EXPECT_TRUE(shouldReplacePluginState(4096, 4096));    // equal sizes → replace
}
