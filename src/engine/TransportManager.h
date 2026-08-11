#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <optional>
#include <shared_mutex>
#include <map>
#include <cmath>

namespace HDAW {

struct ArrangerRegionData {
    juce::String regionID;
    double startTime = 0.0;  // beats
    double duration = 0.0;   // beats
};

struct ArrangerChainEntryData {
    int regionIndex = -1;    // index into regions vector
    int repeatCount = 1;
};

struct ArrangerChainData {
    std::vector<ArrangerRegionData> regions;
    std::vector<ArrangerChainEntryData> entries;
    double totalDurationBeats = 0.0;
};

struct ChainPosition {
    int entryIndex = 0;
    int repeatIndex = 0;
    double timelineBeat = 0.0;
};

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

    void setProjectEndSample(int64_t sample) { projectEndSample.store(sample); }
    int64_t getProjectEndSample() const { return projectEndSample.load(); }

    void setPunchEnabled(bool enabled) { punchEnabled.store(enabled); }
    bool isPunchEnabled() const { return punchEnabled.load(); }

    void setArrangerEnabled(bool v) { arrangerEnabled.store(v); }
    bool isArrangerEnabled() const { return arrangerEnabled.load(); }
    void setArrangerChainPosition(int v) { arrangerChainPosition.store(v); }
    int getArrangerChainPosition() const { return arrangerChainPosition.load(); }
    void setArrangerRepeatIndex(int v) { arrangerRepeatIndex.store(v); }
    int getArrangerRepeatIndex() const { return arrangerRepeatIndex.load(); }

    void rebuildArrangerChainData(const juce::ValueTree& root);

    ChainPosition resolveChainPosition(double beat) const
    {
        std::shared_lock lock(arrangerChainMutex);
        return resolveChainPositionImpl(beat);
    }

    // Auto-stop flag: set by the audio thread in advance() when position
    // exceeds project end. The message thread must observe this and fire
    // the proper stop command (ValueTree update + UI notification).
    bool consumeAutoStopRequested() { return autoStopRequested.exchange(false); }

    bool consumePunchOutRequested() { return punchOutRequested.exchange(false); }
    void requestPunchOut() { punchOutRequested.store(true); }

    // Returns true if auto-stop fired (position exceeded project end).
    bool advance(int numSamples)
    {
        if (isPlaying.load() || isRecording.load())
        {
            int64_t newPos = currentSample.fetch_add(numSamples) + numSamples;

            // Arranger mode: remap virtual position through chain order
            if (arrangerEnabled.load())
            {
                std::shared_lock lock(arrangerChainMutex);
                if (arrangerChainData && !arrangerChainData->entries.empty()
                    && arrangerChainData->totalDurationBeats > 0)
                {
                    const double sr = sampleRate.load();
                    const double bpmVal = bpm.load();
                    if (sr > 0 && bpmVal > 0)
                    {
                        const double samplesPerBeat = 60.0 * sr / bpmVal;
                        double virtualBeat = static_cast<double>(newPos) / samplesPerBeat;

                        // Wrap around chain total duration
                        const double chainDur = arrangerChainData->totalDurationBeats;
                        virtualBeat = std::fmod(virtualBeat, chainDur);
                        if (virtualBeat < 0.0)
                            virtualBeat += chainDur;

                        // Resolve chain position (inline, no second lock)
                        ChainPosition pos = resolveChainPositionImpl(virtualBeat);

                        const int64_t remappedSample = static_cast<int64_t>(pos.timelineBeat * samplesPerBeat);
                        currentSample.store(remappedSample);

                        arrangerChainPosition.store(pos.entryIndex);
                        arrangerRepeatIndex.store(pos.repeatIndex);
                    }
                }
                return false;
            }

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
                return false;
            }
            else
            {
                int64_t projEnd = projectEndSample.load();
                if (projEnd > 0 && newPos >= projEnd)
                {
                    currentSample.store(projEnd);
                    isPlaying.store(false);
                    autoStopRequested.store(true);
                    return true;
                }
            }
        }
        return false;
    }

    double getSampleRate() const { return sampleRate.load(); }
    double getBPM() const { return bpm.load(); }

    // Get current position in seconds
    double getCurrentPositionSeconds() const
    {
        const int64_t currentSample = getCurrentSample();
        const double sr = getSampleRate();
        return sr > 0 ? static_cast<double>(currentSample) / sr : 0.0;
    }

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
    ChainPosition resolveChainPositionImpl(double beat) const
    {
        ChainPosition result;
        if (!arrangerChainData || arrangerChainData->entries.empty())
        {
            result.timelineBeat = beat;
            return result;
        }

        double accumulated = 0.0;
        for (int i = 0; i < static_cast<int>(arrangerChainData->entries.size()); ++i)
        {
            const auto& entry = arrangerChainData->entries[i];
            const auto& region = arrangerChainData->regions[entry.regionIndex];
            const double entryDuration = region.duration * entry.repeatCount;
            if (beat >= accumulated && beat < accumulated + entryDuration)
            {
                const double offsetInEntry = beat - accumulated;
                const int repeatIdx = static_cast<int>(offsetInEntry / region.duration);
                const double offsetInRepeat = offsetInEntry - (repeatIdx * region.duration);
                result.entryIndex = i;
                result.repeatIndex = repeatIdx;
                result.timelineBeat = region.startTime + offsetInRepeat;
                return result;
            }
            accumulated += entryDuration;
        }

        // Past chain end — clamp to last entry's end
        const int lastIdx = static_cast<int>(arrangerChainData->entries.size()) - 1;
        const auto& lastEntry = arrangerChainData->entries[lastIdx];
        const auto& lastRegion = arrangerChainData->regions[lastEntry.regionIndex];
        result.entryIndex = lastIdx;
        result.repeatIndex = lastEntry.repeatCount - 1;
        result.timelineBeat = lastRegion.startTime + lastRegion.duration;
        return result;
    }

    std::atomic<bool> isPlaying { false };
    std::atomic<bool> isRecording { false };
    std::atomic<bool> isLooping { false };
    std::atomic<int64_t> currentSample { 0 };
    std::atomic<int64_t> loopStartSample { 0 };
    std::atomic<int64_t> loopEndSample { 0 };
    std::atomic<int64_t> projectEndSample { 0 };
    std::atomic<bool> punchEnabled { false };
    std::atomic<bool> autoStopRequested { false };
    std::atomic<bool> punchOutRequested { false };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> bpm { 120.0 };
    std::shared_ptr<const std::vector<TempoPoint>> tempoMap;

    // Arranger mode
    std::atomic<bool> arrangerEnabled { false };
    std::atomic<int> arrangerChainPosition { 0 };
    std::atomic<int> arrangerRepeatIndex { 0 };
    std::shared_ptr<const ArrangerChainData> arrangerChainData;
    mutable std::shared_mutex arrangerChainMutex;
};

inline void TransportManager::rebuildArrangerChainData(const juce::ValueTree& root)
{
    auto data = std::make_shared<ArrangerChainData>();

    auto arrangerList = root.getChildWithName(juce::Identifier("ARRANGER_LIST"));
    auto chainList = root.getChildWithName(juce::Identifier("ARRANGER_CHAIN_LIST"));
    if (!arrangerList.isValid() || !chainList.isValid())
    {
        std::unique_lock lock(arrangerChainMutex);
        arrangerChainData = data;
        return;
    }

    // Find active chain
    juce::ValueTree activeChain;
    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto chain = chainList.getChild(i);
        if (static_cast<bool>(chain.getProperty(juce::Identifier("isActive"), false)))
        {
            activeChain = chain;
            break;
        }
    }
    if (!activeChain.isValid() && chainList.getNumChildren() > 0)
        activeChain = chainList.getChild(0);

    if (!activeChain.isValid())
    {
        std::unique_lock lock(arrangerChainMutex);
        arrangerChainData = data;
        return;
    }

    // Build region index
    std::map<juce::String, int> regionIndexMap;
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto region = arrangerList.getChild(i);
        ArrangerRegionData rd;
        rd.regionID = region.getProperty(juce::Identifier("regionID"), "").toString();
        rd.startTime = static_cast<double>(region.getProperty(juce::Identifier("startTime"), 0.0));
        rd.duration = static_cast<double>(region.getProperty(juce::Identifier("duration"), 0.0));
        regionIndexMap[rd.regionID] = static_cast<int>(data->regions.size());
        data->regions.push_back(rd);
    }

    // Build entries
    double totalBeats = 0.0;
    for (int i = 0; i < activeChain.getNumChildren(); ++i)
    {
        auto entry = activeChain.getChild(i);
        juce::String rid = entry.getProperty(juce::Identifier("regionID"), "").toString();
        auto it = regionIndexMap.find(rid);
        if (it == regionIndexMap.end()) continue;

        ArrangerChainEntryData ed;
        ed.regionIndex = it->second;
        ed.repeatCount = juce::jmax(1, static_cast<int>(entry.getProperty(juce::Identifier("repeatCount"), 1)));
        data->entries.push_back(ed);
        totalBeats += data->regions[ed.regionIndex].duration * ed.repeatCount;
    }
    data->totalDurationBeats = totalBeats;

    std::unique_lock lock(arrangerChainMutex);
    arrangerChainData = data;
}

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
