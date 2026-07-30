#pragma once
#include <atomic>
#include <vector>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace HDAW {

class LevelMeter
{
public:
    LevelMeter()
    {
        leftLevel.store(0.0f);
        rightLevel.store(0.0f);
        rmsLeft.store(0.0f);
        rmsRight.store(0.0f);
        lufsValue.store(-70.0f);
    }

    void prepare(double sampleRate, int samplesPerBlock = 512)
    {
        // EMA coefficient adjusted for per-block updates (time constant 0.3s)
        float blockRate = static_cast<float>(sampleRate / std::max(1, samplesPerBlock));
        rmsCoeff = 1.0f - std::exp(-1.0f / (blockRate * 0.3f));
        prepareLufs(sampleRate);
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

        // RMS: exponential moving average
        if (rmsCoeff > 0.0f && numSamples > 0)
        {
            float sumL = 0.0f, sumR = 0.0f;
            for (int s = 0; s < numSamples; ++s)
            {
                float l = buffer.getSample(0, s);
                sumL += l * l;
                if (numChannels > 1)
                {
                    float r = buffer.getSample(1, s);
                    sumR += r * r;
                }
            }
            float rmsL_new = std::sqrt(sumL / numSamples);
            float rmsR_new = (numChannels > 1) ? std::sqrt(sumR / numSamples) : rmsL_new;

            float prevL = rmsLeft.load(std::memory_order_relaxed);
            float prevR = rmsRight.load(std::memory_order_relaxed);
            rmsLeft.store(prevL + rmsCoeff * (rmsL_new - prevL), std::memory_order_relaxed);
            rmsRight.store(prevR + rmsCoeff * (rmsR_new - prevR), std::memory_order_relaxed);
        }

        // LUFS momentary (K-weighted, 400ms window)
        if (kwInitialized && numSamples > 0)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                float l = kwHighshelf.process(buffer.getSample(0, s));
                l = kwHighpass.process(l);
                float sq = l * l;
                if (numChannels > 1)
                {
                    float r = kwHighshelf.process(buffer.getSample(1, s));
                    r = kwHighpass.process(r);
                    sq = 0.5f * (sq + r * r);
                }
                lufsSum -= lufsBuffer[lufsWriteIdx];
                lufsBuffer[lufsWriteIdx] = sq;
                lufsSum += sq;
                lufsWriteIdx = (lufsWriteIdx + 1) % lufsWindowSize;
            }

            double meanSquare = lufsSum / lufsWindowSize;
            float lufs = (meanSquare < 1e-20) ? -70.0f
                         : static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
            lufsValue.store(lufs, std::memory_order_relaxed);
        }
    }

    float getLeftLevel() const { return leftLevel.load(); }
    float getRightLevel() const { return rightLevel.load(); }
    float getRmsLeft() const { return rmsLeft.load(std::memory_order_relaxed); }
    float getRmsRight() const { return rmsRight.load(std::memory_order_relaxed); }
    float getLufsMomentary() const { return lufsValue.load(std::memory_order_relaxed); }

private:
    std::atomic<float> leftLevel;
    std::atomic<float> rightLevel;

    // RMS
    std::atomic<float> rmsLeft;
    std::atomic<float> rmsRight;
    float rmsCoeff{0.0f};

    // LUFS momentary (K-weighted, 400ms sliding window)
    struct KWeightFilter {
        float b0, b1, b2, a1, a2;
        float x1=0, x2=0, y1=0, y2=0;
        float process(float x) {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
    };
    KWeightFilter kwHighshelf{};
    KWeightFilter kwHighpass{};
    bool kwInitialized = false;

    std::vector<float> lufsBuffer;
    int lufsWriteIdx = 0;
    int lufsWindowSize = 0;
    double lufsSum = 0.0;
    std::atomic<float> lufsValue;

    void prepareLufs(double sampleRate)
    {
        lufsWindowSize = static_cast<int>(sampleRate * 400 / 1000);
        if (lufsWindowSize < 1) lufsWindowSize = 1;
        lufsBuffer.resize(lufsWindowSize, 0.0f);
        lufsWriteIdx = 0;
        lufsSum = 0.0;
        kwHighshelf = {1.53512485958697f, -2.69169618940638f, 1.19839281085285f,
                       -1.69065929318241f, 0.73248077421585f};
        kwHighpass = {1.0f, -2.0f, 1.0f,
                      -1.99004745483398f, 0.99007225036621f};
        kwInitialized = true;
    }
};

} // namespace HDAW
