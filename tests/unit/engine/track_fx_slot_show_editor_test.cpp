#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "ui/DebugLog.h"
#include <memory>
#include <vector>

// Helper: juce::String -> std::string -> QString for HDAW_LOG
static inline QString j2q(const juce::String& s) {
    return QString::fromStdString(s.toStdString());
}

// Minimal mock juce::AudioProcessorEditor. We override paint and
// setSize in the ctor so the PluginEditorWindow has a size to
// centre on.
class FakeEditor : public juce::AudioProcessorEditor {
public:
    explicit FakeEditor(juce::AudioProcessor& p) : juce::AudioProcessorEditor(p) {
        setSize(400, 300);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
};

// Minimal mock juce::AudioPluginInstance. We override only the
// virtuals that the compiler confirms exist on the base in this
// JUCE 8 build. Methods that the compiler rejects with
// "did not override any base class methods" are stubbed without
// the `override` keyword (or removed entirely).
class FakePlugin : public juce::AudioPluginInstance {
public:
    FakePlugin()
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withInput("In", juce::AudioChannelSet::mono())
                  .withOutput("Out", juce::AudioChannelSet::mono())) {}

    const juce::String getName() const override { return "Fake"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() {
        return new FakeEditor(*this);
    }
    bool hasEditor() const override { return true; }
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
        d.name = "Fake";
        d.fileOrIdentifier = "Fake";
        return d;
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Fake"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override { destData.setSize(0); }
    void setStateInformation(const void* data, int sizeInBytes) override { (void)data; (void)sizeInBytes; }
    void fillInPluginDescription(juce::PluginDescription& d) const override { d = getPluginDescription(); }
};

TEST(TrackFXSlotShowEditor, ShowEditorTriggersEditorCreation) {
    HDAW_LOG("ShowEditorTest", j2q("case1: creating FakePlugin"));
    auto plugin = std::make_unique<FakePlugin>();
    HDAW::TrackFXSlot slot(std::move(plugin), "Fake");
    HDAW_LOG("ShowEditorTest", j2q("case1: ctor done; isEditorOpen=" +
        juce::String(slot.isEditorOpen() ? "true" : "false") + " (expect false)"));
    EXPECT_FALSE(slot.isEditorOpen());

    HDAW_LOG("ShowEditorTest", j2q("case1: calling showEditor"));
    slot.showEditor();
    HDAW_LOG("ShowEditorTest", j2q("case1: isEditorOpen=" +
        juce::String(slot.isEditorOpen() ? "true" : "false") + " (expect true)"));
    EXPECT_TRUE(slot.isEditorOpen());

    HDAW_LOG("ShowEditorTest", j2q("case1: slot will now be destroyed; check FXSlotDtor log for rawPtr"));
}
