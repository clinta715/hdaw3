#include "gtest/gtest.h"
#include "engine/TransientDetector.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>

TEST(TransientDetector, DetectTransientsInSignal)
{
    // Generate test signal: silence + sine burst + silence
    const int sr = 44100;
    const int len = sr * 2;  // 2 seconds
    juce::AudioBuffer<float> buf(1, len);
    buf.clear();
    
    // Verify buffer is cleared
    std::cout << "TEST: buf.getNumSamples() = " << buf.getNumSamples() << std::endl;
    std::cout << "TEST: buf[0][0] = " << buf.getSample(0, 0) << std::endl;
    std::cout << "TEST: buf[0][100] = " << buf.getSample(0, 100) << std::endl;
    
    // Add sine burst at 0.5s: 220Hz for 50ms
    const int t1 = sr / 2;
    const int burstLen = sr / 20;  // 50ms
    for (int i = 0; i < burstLen; ++i)
    {
        buf.setSample(0, t1 + i, 
            std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * i / sr));
    }
    
    // Add sine burst at 1.5s: 440Hz for 50ms
    const int t2 = sr + sr / 2;
    for (int i = 0; i < burstLen; ++i)
    {
        buf.setSample(0, t2 + i,
            std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / sr));
    }
    
    // Verify burst was written
    std::cout << "TEST: buf[0][" << t1 << "] = " << buf.getSample(0, t1) << std::endl;
    std::cout << "TEST: buf[0][" << t2 << "] = " << buf.getSample(0, t2) << std::endl;
    
    HDAW::TransientDetector detector;
    auto transients = detector.detect(buf, sr);
    
    std::cout << "Detected " << transients.transientTimes.size() << " transients:" << std::endl;
    for (double t : transients.transientTimes)
    {
        std::cout << "  " << t << "s" << std::endl;
    }
    
    // Should detect at least 2 transients (onset of each burst)
    EXPECT_GE(transients.transientTimes.size(), 2);
    
    // Check that at least one transient is near each burst onset
    bool foundNear05 = false;
    bool foundNear15 = false;
    for (double t : transients.transientTimes)
    {
        if (std::abs(t - 0.5) < 0.02) foundNear05 = true;
        if (std::abs(t - 1.5) < 0.02) foundNear15 = true;
    }
    EXPECT_TRUE(foundNear05) << "No transient detected near 0.5s";
    EXPECT_TRUE(foundNear15) << "No transient detected near 1.5s";
}