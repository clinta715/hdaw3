#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"

namespace
{
class ProgramMockPlugin : public juce::AudioPluginInstance
{
public:
    ProgramMockPlugin()
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withInput("In", juce::AudioChannelSet::mono())
                  .withOutput("Out", juce::AudioChannelSet::mono())) {}
    ~ProgramMockPlugin() override = default;

    const juce::String getName() const override { return "ProgramMock"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumParameters() override { return 0; }
    float getParameter(int) override { return 0; }
    void setParameter(int, float) override {}
    const juce::String getParameterName(int) override { return ""; }
    const juce::String getParameterText(int) override { return ""; }
    const juce::String getInputChannelName(int) const override { return ""; }
    const juce::String getOutputChannelName(int) const override { return ""; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "ProgramMock";
        d.fileOrIdentifier = "ProgramMock";
    }
    int getNumPrograms() override { return 3; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int index) override
    {
        static const juce::String names[] = { "Init", "Warm", "Bright" };
        return names[static_cast<size_t>(index) % 3u];
    }
    void changeProgramName(int, const juce::String&) override {}
};

void addProgramSlot(HDAW::Track* tr)
{
    auto& chain = tr->getFXChain();
    chain.push_back(std::make_unique<HDAW::TrackFXSlot>(
        std::make_unique<ProgramMockPlugin>(), juce::String("ProgramMock")));
}

void addInternalSlot(HDAW::Track* tr)
{
    auto& chain = tr->getFXChain();
    chain.push_back(std::make_unique<HDAW::TrackFXSlot>(juce::String("eq")));
}
} // namespace

TEST(AudioEngineReadFacadeTest, GetFxProgramListIsTypedAndReturnsData)
{
    AudioEngine engine;
    engine.initialize();
    auto* tr = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr, nullptr);
    addProgramSlot(tr);

    auto progs = engine.getFxProgramList(0, 0);
    ASSERT_EQ(progs.size(), 3u);
    EXPECT_EQ(progs[0].index, 0);
    EXPECT_EQ(progs[1].index, 1);
    EXPECT_EQ(progs[2].index, 2);
    EXPECT_FALSE(progs[0].name.empty());
    EXPECT_EQ(progs[0].name, "Init");
    EXPECT_EQ(progs[1].name, "Warm");
    EXPECT_EQ(progs[2].name, "Bright");
}

TEST(AudioEngineReadFacadeTest, GetFxProgramListReturnsEmptyForNonPluginAndInvalidSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto* tr = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr, nullptr);
    addInternalSlot(tr);

    EXPECT_TRUE(engine.getFxProgramList(0, 0).empty());
    EXPECT_TRUE(engine.getFxProgramList(0, 9).empty());
    EXPECT_TRUE(engine.getFxProgramList(99, 0).empty());
}