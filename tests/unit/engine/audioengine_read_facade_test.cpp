#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"

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

// Writes a temporary mono WAV filled with a 220 Hz sine at -6 dBFS and returns
// its path. Caller is responsible for deleting the file.
juce::File makeSineWav(double seconds, int sampleRate = 44100)
{
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::SpecialLocationType::tempDirectory);
    auto f = tempDir.getNonexistentChildFile("hdaw_read_facade", ".wav", false);

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    jassert(fos != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w(
        fmt.createWriterFor(fos.get(), sampleRate, 1, 16, {}, 0));
    fos.release(); // writer owns it now

    const int total = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buf(1, total);
    const double amp = 0.5;
    for (int i = 0; i < total; ++i)
        buf.setSample(0, i, static_cast<float>(amp * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate)));
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return f;
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

TEST(AudioEngineReadFacadeTest, GetWaveformPeaksReturnsBinData)
{
    auto wav = makeSineWav(1.0, 44100);
    AudioEngine engine;
    engine.initialize();

    auto& pm = engine.getProjectModel();
    auto clip = pm.createAudioClip("Wave", 0.0, 1.0, wav.getFullPathName());
    int clipId = clip.getProperty(IDs::clipID);
    auto clipList = pm.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    clipList.addChild(clip, -1, nullptr);

    const int numBins = 100;
    auto peaks = engine.getWaveformPeaks(clipId, numBins);
    wav.deleteFile();

    ASSERT_TRUE(peaks.ok) << peaks.error;
    EXPECT_EQ(peaks.peaks.size(), static_cast<size_t>(numBins) * 2u);
    EXPECT_DOUBLE_EQ(peaks.sampleRate, 44100.0);
    EXPECT_EQ(peaks.numSamples, 44100);
    // A sine's first bin spans the rising edge, so max should exceed min.
    EXPECT_GT(peaks.peaks[1], peaks.peaks[0]);
}

TEST(AudioEngineReadFacadeTest, GetWaveformPeaksRejectsMissingOrNonAudioClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& pm = engine.getProjectModel();

    // Unknown clip id.
    auto missing = engine.getWaveformPeaks(999999, 10);
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(missing.errorCode, -32602);

    // A MIDI clip is not an audio clip.
    auto midi = pm.createMidiClipEmpty("midi", 0, 1);
    int midiId = midi.getProperty(IDs::clipID);
    auto clipList = pm.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    clipList.addChild(midi, -1, nullptr);
    auto notAudio = engine.getWaveformPeaks(midiId, 10);
    EXPECT_FALSE(notAudio.ok);
    EXPECT_EQ(notAudio.errorCode, -32602);
}