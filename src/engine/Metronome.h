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
        const double freq = isDownbeat ? 1500.0 : 880.0;
        const float amp = isDownbeat ? 0.4f : 0.25f;
        const int numChannels = buffer.getNumChannels();
        const int bufferSize = buffer.getNumSamples();

        for (int s = 0; s < clickSamples && (offset + s) < bufferSize; ++s)
        {
            const double t = static_cast<double>(s) / sr;
            const float env = amp * static_cast<float>(std::exp(-t * 80.0));
            const float sample = env * static_cast<float>(
                std::sin(2.0 * 3.14159265358979 * freq * t));

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addSample(ch, offset + s, sample);
        }
    }

    double sr = 44100.0;
    int clickSamples = 1102;
    std::atomic<bool> enabled{ false };
    std::atomic<int> beatsPerBar{ 4 };
};

} // namespace HDAW
