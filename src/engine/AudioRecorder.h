#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <memory>

namespace HDAW {

class AudioRecorder
{
public:
    AudioRecorder();
    ~AudioRecorder();

    bool startRecording(const juce::File& file, double sampleRate, int numChannels);
    juce::File stopRecording();

    bool isRecording() const { return active.load(); }
    void processBlock(const juce::AudioBuffer<float>& buffer);

private:
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::TimeSliceThread backgroundThread{ "Recording Thread" };
    juce::AudioBuffer<float> channelBuffer;
    int numChannels = 0;

    std::atomic<bool> active{ false };
    std::atomic<bool> inProcessBlock{ false };
    juce::File outputFile;

    std::array<const float*, 2> channelPtrs{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};

} // namespace HDAW
