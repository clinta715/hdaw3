#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

namespace HDAW {

class TransientDetector
{
public:
    struct Result
    {
        std::vector<double> transientTimes;  // in seconds
    };
    
    // Synchronous detection (for testing/simple use)
    Result detect(const juce::AudioBuffer<float>& buffer, double sampleRate);
    
    // Async detection (off-thread, like StretchRenderer)
    using DetectCallback = std::function<void(const Result&)>;
    bool startDetectFromFile(const juce::String& filePath, 
                              juce::AudioFormatManager& formatManager,
                              DetectCallback callback);

private:
    static constexpr int FFT_SIZE = 1024;
    static constexpr int HOP_SIZE = 256;
    
    Result detectFromBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);
    double spectralFlux(const std::vector<float>& frame, const std::vector<float>& prevFrame) const;
    
    std::atomic<bool> active{false};
    std::atomic<bool> cancelFlag{false};
    std::thread detectThread;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager* formatManager = nullptr;
    DetectCallback completionCallback;
    juce::String sourceFile;
    
    void detectThreadFunc();
};

} // namespace HDAW