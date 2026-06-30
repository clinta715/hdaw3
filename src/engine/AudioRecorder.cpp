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

    auto wavFormat = std::make_unique<juce::WavAudioFormat>();
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

    wavFormat.release();

    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        wavWriter, backgroundThread, 65536);

    channelBuffer.setSize(numChannels, 16384);
    channelBuffer.clear();
    active = true;

    return true;
}

juce::File AudioRecorder::stopRecording()
{
    if (!active.load())
        return {};

    active = false;

    int waitMs = 0;
    while (inProcessBlock.load() && waitMs < 200)
    {
        juce::Thread::sleep(1);
        ++waitMs;
    }

    threadedWriter.reset();

    return outputFile;
}

void AudioRecorder::processBlock(const juce::AudioBuffer<float>& buffer)
{
    if (!active.load())
        return;

    inProcessBlock.store(true);

    int samples = buffer.getNumSamples();
    int chans = juce::jmin(buffer.getNumChannels(), numChannels);

    if (samples <= 0)
    {
        inProcessBlock.store(false);
        return;
    }

    samples = juce::jmin(samples, channelBuffer.getNumSamples());

    for (int ch = 0; ch < chans; ++ch)
        channelBuffer.copyFrom(ch, 0, buffer, ch, 0, samples);

    for (int ch = 0; ch < chans; ++ch)
        channelPtrs[ch] = channelBuffer.getReadPointer(ch);

    if (threadedWriter != nullptr)
        threadedWriter->write(channelPtrs.data(), samples);

    inProcessBlock.store(false);
}

} // namespace HDAW
