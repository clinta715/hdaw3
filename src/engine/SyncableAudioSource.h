#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include "TransportManager.h"

namespace HDAW {

class SyncableAudioSource : public juce::AudioSource
{
public:
    SyncableAudioSource() = default;
    ~SyncableAudioSource() override = default;

    void prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate) override
    {
        deviceSampleRate = sampleRate;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        if (!playing.load(std::memory_order_relaxed) || lengthInSamples == 0)
        {
            info.clearActiveBufferRegion();
            return;
        }

        double filePos;
        if (transport != nullptr && transport->isPlayingNow())
        {
            int64_t transportSample = transport->getCurrentSample();
            double transportSr = transport->getSampleRate();
            if (transportSr <= 0.0) transportSr = deviceSampleRate;
            filePos = static_cast<double>(transportSample)
                      * (fileSampleRate / transportSr) * resamplingRatio;
        }
        else
        {
            int64_t pos = internalPos.load(std::memory_order_relaxed);
            filePos = static_cast<double>(pos)
                      * (fileSampleRate / deviceSampleRate) * resamplingRatio;
            internalPos.fetch_add(info.numSamples, std::memory_order_relaxed);

            double maxPos = static_cast<double>(lengthInSamples) / resamplingRatio
                            * (deviceSampleRate / fileSampleRate);
            if (static_cast<double>(internalPos.load(std::memory_order_relaxed)) >= maxPos)
                playing.store(false, std::memory_order_relaxed);
        }

        readBlock(filePos, info);
    }

    bool loadFile(const juce::File& file, juce::AudioFormatManager& fm)
    {
        playing.store(false, std::memory_order_relaxed);
        internalPos.store(0, std::memory_order_relaxed);
        lengthInSamples = 0;

        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr) return false;

        fileSampleRate = reader->sampleRate;
        lengthInSamples = static_cast<int64_t>(reader->lengthInSamples);
        if (lengthInSamples <= 0) return false;

        int numChannels = static_cast<int>(reader->numChannels);
        for (int ch = 0; ch < 2; ++ch)
            buffer[ch].allocate(static_cast<size_t>(lengthInSamples), true);

        const int numSamples = static_cast<int>(lengthInSamples);
        if (numChannels == 1)
        {
            float* dests[1] = { buffer[0].getData() };
            reader->read(dests, 1, 0, numSamples);
            std::memcpy(buffer[1].getData(), buffer[0].getData(),
                        static_cast<size_t>(lengthInSamples) * sizeof(float));
        }
        else
        {
            float* dests[2] = { buffer[0].getData(), buffer[1].getData() };
            reader->read(dests, 2, 0, numSamples);
        }
        return true;
    }

    void play()  { internalPos.store(0, std::memory_order_relaxed); playing.store(true, std::memory_order_relaxed); }
    void stop()  { playing.store(false, std::memory_order_relaxed); }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }

    void setTransportManager(TransportManager* tm) { transport = tm; }
    void setResamplingRatio(double ratio) { resamplingRatio = juce::jlimit(0.25, 4.0, ratio); }
    void setGain(float g) { gain.store(g, std::memory_order_relaxed); }

    double getLengthInSeconds() const
    {
        if (fileSampleRate <= 0.0 || lengthInSamples == 0) return 0.0;
        return static_cast<double>(lengthInSamples) / fileSampleRate;
    }

private:
    void readBlock(double filePos, const juce::AudioSourceChannelInfo& info)
    {
        auto& outBuffer = *info.buffer;
        float g = gain.load(std::memory_order_relaxed);

        for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
        {
            float* dest = outBuffer.getWritePointer(ch, info.startSample);
            const float* src = buffer[ch < 2 ? ch : 0].getData();

            for (int i = 0; i < info.numSamples; ++i)
            {
                double pos = filePos + static_cast<double>(i) * resamplingRatio;
                int64_t idx = static_cast<int64_t>(pos);

                if (idx < 0 || idx >= lengthInSamples)
                {
                    dest[i] = 0.0f;
                }
                else if (idx + 1 < lengthInSamples)
                {
                    double frac = pos - static_cast<double>(idx);
                    dest[i] = static_cast<float>(
                        (src[idx] * (1.0 - frac) + src[idx + 1] * frac) * g);
                }
                else
                {
                    dest[i] = src[idx] * g;
                }
            }
        }
    }

    juce::HeapBlock<float> buffer[2];
    int64_t lengthInSamples = 0;
    double fileSampleRate = 44100.0;
    double deviceSampleRate = 44100.0;
    double resamplingRatio = 1.0;
    std::atomic<int64_t> internalPos{0};
    std::atomic<bool> playing{false};
    std::atomic<float> gain{0.8f};
    TransportManager* transport = nullptr;
};

} // namespace HDAW
