#include "AudioRecorder.h"

namespace HDAW {

AudioRecorder::AudioRecorder()
{
    backgroundThread.startThread();
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    backgroundThread.stopThread(500);
}

bool AudioRecorder::startRecording(const juce::File& file, double sampleRate, int numInputChannels)
{
    stopRecording();

    outputFile = file;
    outputFile.getParentDirectory().createDirectory();
    numChannels = numInputChannels;

    auto* wavFormat = new juce::WavAudioFormat();
    auto* outStream = file.createOutputStream().release();

    if (outStream == nullptr)
        return false;

    auto* wavWriter = wavFormat->createWriterFor(
        outStream, sampleRate, numChannels, 16, {}, 0);

    if (wavWriter == nullptr)
    {
        delete outStream;
        return false;
    }

    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        wavWriter, backgroundThread, 65536);

    channelBuffer.setSize(numChannels, 4096);
    active = true;

    return true;
}

juce::File AudioRecorder::stopRecording()
{
    if (!active.load())
        return {};

    active = false;

    threadedWriter.reset();

    return outputFile;
}

void AudioRecorder::processBlock(const juce::AudioBuffer<float>& buffer)
{
    if (!active.load())
        return;

    int samples = buffer.getNumSamples();
    int chans = juce::jmin(buffer.getNumChannels(), numChannels);

    if (samples <= 0) return;

    channelBuffer.setSize(chans, samples, false, false, true);

    for (int ch = 0; ch < chans; ++ch)
        channelBuffer.copyFrom(ch, 0, buffer, ch, 0, samples);

    std::vector<const float*> channelPtrs(chans);
    for (int ch = 0; ch < chans; ++ch)
        channelPtrs[ch] = channelBuffer.getReadPointer(ch);

    threadedWriter->write(channelPtrs.data(), samples);
}

} // namespace HDAW
