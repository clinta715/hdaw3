#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <optional>

namespace HDAW {

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

    void advance(int numSamples)
    {
        if (isPlaying.load() || isRecording.load())
            currentSample.fetch_add(numSamples);
    }

    double getSampleRate() const { return sampleRate.load(); }
    double getBPM() const { return bpm.load(); }

private:
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> isRecording { false };
    std::atomic<int64_t> currentSample { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> bpm { 120.0 };
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
        info.setBpm(transportManager.getBPM());
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
        
        const double sr = transportManager.getSampleRate();
        const int64_t currentSample = transportManager.getCurrentSample();
        
        info.setFrameRate(juce::AudioPlayHead::FrameRate().withBaseRate(30));
        info.setIsPlaying(transportManager.isPlayingNow());
        info.setTimeInSamples(currentSample);
        info.setTimeInSeconds(static_cast<double>(currentSample) / sr);
        
        const double secondsPerBeat = 60.0 / transportManager.getBPM();
        const double seconds = static_cast<double>(currentSample) / sr;
        const double ppqPosition = seconds / secondsPerBeat;
        info.setPpqPosition(ppqPosition);
        
        return info;
    }

private:
    const TransportManager& transportManager;
};

} // namespace HDAW
