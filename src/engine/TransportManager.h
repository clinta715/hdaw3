#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <optional>

namespace HDAW {

struct TempoPoint {
    double timeInSeconds;
    double bpm;
};

class TransportManager
{
public:
    TransportManager() = default;

    void setSampleRate(double newSampleRate) { sampleRate.store(newSampleRate); }
    void setBPM(double newBPM) { bpm.store(newBPM); }

    void setPlaying(bool shouldPlay) { isPlaying.store(shouldPlay); }
    bool isPlayingNow() const { return isPlaying.load(); }

    void setRecording(bool shouldRecord) { isRecording.store(shouldRecord); }
    bool isRecordingNow() const { return isRecording.load(); }

    void setCurrentSample(int64_t sample) { currentSample.store(sample); }
    int64_t getCurrentSample() const { return currentSample.load(); }

    void setLooping(bool shouldLoop) { isLooping.store(shouldLoop); }
    bool isLoopingNow() const { return isLooping.load(); }

    void setLoopStartSample(int64_t sample) { loopStartSample.store(sample); }
    int64_t getLoopStartSample() const { return loopStartSample.load(); }

    void setLoopEndSample(int64_t sample) { loopEndSample.store(sample); }
    int64_t getLoopEndSample() const { return loopEndSample.load(); }

    void advance(int numSamples)
    {
        if (isPlaying.load() || isRecording.load())
        {
            int64_t newPos = currentSample.fetch_add(numSamples) + numSamples;
            if (isLooping.load())
            {
                int64_t end = loopEndSample.load();
                if (end > 0 && newPos >= end)
                {
                    int64_t start = loopStartSample.load();
                    int64_t loopLen = end - start;
                    if (loopLen > 0)
                    {
                        int64_t offset = (newPos - start) % loopLen;
                        currentSample.store(start + offset);
                    }
                }
            }
        }
    }

    double getSampleRate() const { return sampleRate.load(); }
    double getBPM() const { return bpm.load(); }

    void setTempoMap(std::shared_ptr<const std::vector<TempoPoint>> map)
    {
        std::atomic_store(&tempoMap, std::move(map));
    }

    double getBpmAtTime(double timeInSeconds) const
    {
        auto map = std::atomic_load(&tempoMap);
        if (!map || map->empty())
            return bpm.load();

        const TempoPoint* last = nullptr;
        for (const auto& pt : *map)
        {
            if (pt.timeInSeconds > timeInSeconds) break;
            last = &pt;
        }
        return last ? last->bpm : map->front().bpm;
    }

    double samplesToPpq(int64_t sample) const
    {
        const double sr = sampleRate.load();
        return secondsToPpq(static_cast<double>(sample) / sr);
    }

    double secondsToPpq(double sec) const
    {
        auto map = std::atomic_load(&tempoMap);
        if (!map || map->empty())
            return sec * bpm.load() / 60.0;

        double ppq = 0.0;
        for (size_t i = 0; i < map->size(); ++i)
        {
            const auto& pt = map->at(i);
            if (sec <= pt.timeInSeconds) break;

            double segEnd = (i + 1 < map->size()) ? map->at(i + 1).timeInSeconds : 1e18;
            double segDur = std::min(sec, segEnd) - pt.timeInSeconds;
            if (segDur > 0.0)
                ppq += segDur * pt.bpm / 60.0;
        }
        return ppq;
    }

private:
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> isRecording { false };
    std::atomic<bool> isLooping { false };
    std::atomic<int64_t> currentSample { 0 };
    std::atomic<int64_t> loopStartSample { 0 };
    std::atomic<int64_t> loopEndSample { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> bpm { 120.0 };
    std::shared_ptr<const std::vector<TempoPoint>> tempoMap;
};

/**
 * Custom PlayHead to provide timing info to JUCE processors/plugins.
 */
class InternalPlayHead : public juce::AudioPlayHead
{
public:
    InternalPlayHead(const TransportManager& tm) : transportManager(tm) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });

        const double sr = transportManager.getSampleRate();
        const int64_t currentSample = transportManager.getCurrentSample();
        const double seconds = static_cast<double>(currentSample) / sr;

        info.setFrameRate(juce::AudioPlayHead::FrameRate().withBaseRate(30));
        info.setIsPlaying(transportManager.isPlayingNow());
        info.setIsLooping(transportManager.isLoopingNow());
        info.setTimeInSamples(currentSample);
        info.setTimeInSeconds(seconds);

        const double ppqPosition = transportManager.samplesToPpq(currentSample);
        info.setPpqPosition(ppqPosition);

        const double currentBpm = transportManager.getBpmAtTime(seconds);
        info.setBpm(currentBpm);

        if (transportManager.isLoopingNow())
        {
            juce::AudioPlayHead::LoopPoints lp;
            lp.ppqStart = transportManager.samplesToPpq(transportManager.getLoopStartSample());
            lp.ppqEnd = transportManager.samplesToPpq(transportManager.getLoopEndSample());
            info.setLoopPoints(lp);
        }

        return info;
    }

private:
    const TransportManager& transportManager;
};

} // namespace HDAW
