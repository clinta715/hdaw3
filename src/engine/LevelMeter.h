#pragma once
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace HDAW {

class LevelMeter
{
public:
    LevelMeter()
    {
        leftLevel.store(0.0f);
        rightLevel.store(0.0f);
    }

    void update(const juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numSamples > 0)
        {
            if (numChannels >= 1)
                leftLevel.store(buffer.getMagnitude(0, 0, numSamples));
            
            if (numChannels >= 2)
                rightLevel.store(buffer.getMagnitude(1, 0, numSamples));
            else if (numChannels >= 1)
                rightLevel.store(leftLevel.load());
        }
    }

    float getLeftLevel() const { return leftLevel.load(); }
    float getRightLevel() const { return rightLevel.load(); }

private:
    std::atomic<float> leftLevel;
    std::atomic<float> rightLevel;
};

} // namespace HDAW
