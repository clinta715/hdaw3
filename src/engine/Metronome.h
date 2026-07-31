#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace HDAW {

class Metronome
{
public:
    void prepareToPlay(double sampleRate)
    {
        sr = sampleRate;
        clickSamples = static_cast<int>(0.025 * sampleRate);

        // Pre-render click waveforms: sine × exponential decay
        // Two clicks: downbeat (1500 Hz) and beat (880 Hz)
        clickBufferDownbeat.setSize(1, clickSamples);
        clickBufferBeat.setSize(1, clickSamples);

        for (int s = 0; s < clickSamples; ++s)
        {
            double t = static_cast<double>(s) / sr;
            float env = static_cast<float>(std::exp(-t * 80.0));
            clickBufferDownbeat.setSample(0, s,
                env * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1500.0 * t)));
            clickBufferBeat.setSample(0, s,
                env * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 880.0 * t)));
        }
    }

    void setEnabled(bool e) { enabled.store(e); }
    bool isEnabled() const { return enabled.load(); }
    void setBeatsPerBar(int bpb) { beatsPerBar.store(bpb); }

    template<typename Transport>
    void processBlock(juce::AudioBuffer<float>& buffer, const Transport& transport)
    {
        if (!enabled.load() || !transport.isPlayingNow()) return;

        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0) return;

        const int64_t startSample = transport.getCurrentSample();

        const double startPpq = transport.samplesToPpq(startSample);
        const double endPpq = transport.samplesToPpq(startSample + numSamples);

        if (endPpq <= startPpq) return;

        const double ppqRange = endPpq - startPpq;
        const int bpb = beatsPerBar.load();

        const int firstBeat = static_cast<int>(std::ceil(startPpq - 0.0001));
        const int lastBeat = static_cast<int>(endPpq + 0.0001);

        for (int beat = firstBeat; beat <= lastBeat; ++beat)
        {
            if (beat < 0) continue;

            const double frac = static_cast<double>(beat - startPpq) / ppqRange;
            const int offset = static_cast<int>(frac * static_cast<double>(numSamples));

            if (offset >= 0 && offset < numSamples)
            {
                const bool isDownbeat = (beat % bpb) == 0;
                renderClick(buffer, offset, isDownbeat);
            }
        }
    }

private:
    void renderClick(juce::AudioBuffer<float>& buffer, int offset, bool isDownbeat)
    {
        const float amp = isDownbeat ? 0.4f : 0.25f;
        const int numChannels = buffer.getNumChannels();
        const int bufferSize = buffer.getNumSamples();

        const juce::AudioBuffer<float>& clickSource = isDownbeat ? clickBufferDownbeat : clickBufferBeat;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = clickSource.getReadPointer(0);
            float* dest = buffer.getWritePointer(ch, offset);
            const int samplesAvailable = bufferSize - offset;
            const int samplesToCopy = (std::min)(clickSamples, samplesAvailable);

            for (int s = 0; s < samplesToCopy; ++s)
                dest[s] += src[s] * amp;
        }
    }

    double sr = 44100.0;
    int clickSamples = 1102;
    std::atomic<bool> enabled{ false };
    std::atomic<int> beatsPerBar{ 4 };

    juce::AudioBuffer<float> clickBufferDownbeat;
    juce::AudioBuffer<float> clickBufferBeat;
};

} // namespace HDAW
