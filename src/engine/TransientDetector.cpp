#include "TransientDetector.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace HDAW {

TransientDetector::~TransientDetector()
{
    cancelFlag = true;
    if (detectThread.joinable())
        detectThread.join();
}

HDAW::TransientDetector::Result HDAW::TransientDetector::detect(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    return detectFromBuffer(buffer, sampleRate);
}

HDAW::TransientDetector::Result HDAW::TransientDetector::detectFromBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Result result;
    if (buffer.getNumSamples() < FFT_SIZE) return result;
    
    // Mix to mono
    juce::AudioBuffer<float> mono(1, buffer.getNumSamples());
    mono.clear();
    if (buffer.getNumChannels() >= 1)
        mono.addFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
    if (buffer.getNumChannels() >= 2)
        mono.addFrom(0, 0, buffer, 1, 0, buffer.getNumSamples());
    mono.applyGain(1.0f / static_cast<float>(std::max(1, buffer.getNumChannels())));
    
    const float* data = mono.getReadPointer(0);
    int numFrames = (buffer.getNumSamples() - FFT_SIZE) / HOP_SIZE;
    
    if (numFrames < 3) return result;  // Need at least 3 frames for peak picking
    
    juce::dsp::FFT fft(10);  // 2^10 = 1024
    std::vector<float> prevFrame(FFT_SIZE/2 + 1, 0.0f);
    std::vector<double> fluxValues;
    fluxValues.reserve(numFrames);
    
    std::vector<float> fftBuffer(FFT_SIZE * 2);
    std::vector<float> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0 * juce::MathConstants<float>::pi * i / (FFT_SIZE - 1)));
    
    for (int frame = 0; frame < numFrames; ++frame)
    {
        int offset = frame * HOP_SIZE;
        
        // Apply window and copy to fftBuffer
        for (int i = 0; i < FFT_SIZE; ++i)
            fftBuffer[i] = data[offset + i] * window[i];
        std::fill(fftBuffer.begin() + FFT_SIZE, fftBuffer.end(), 0.0f);
        
        // FFT
        fft.performRealOnlyForwardTransform(fftBuffer.data());
        
        // Magnitude spectrum
        // JUCE FFT output format: [DC_real, Nyquist_real, Re(1), Im(1), Re(2), Im(2), ...]
        std::vector<float> mag(FFT_SIZE/2 + 1);
        mag[0] = std::abs(fftBuffer[0]);  // DC
        mag[FFT_SIZE/2] = std::abs(fftBuffer[1]);  // Nyquist
        for (int i = 1; i < FFT_SIZE/2; ++i)
        {
            float re = fftBuffer[i * 2];
            float im = fftBuffer[i * 2 + 1];
            mag[i] = std::sqrt(re*re + im*im);
        }
        
        // Spectral flux (positive differences only)
        double flux = 0.0;
        for (int i = 0; i <= FFT_SIZE/2; ++i)
        {
            double diff = mag[i] - prevFrame[i];
            if (diff > 0) flux += diff;
        }
        
        fluxValues.push_back(flux);
        prevFrame = mag;
    }
    
    // Peak picking on flux
    if (fluxValues.size() < 3) return result;
    
    double meanFlux = 0.0;
    for (double f : fluxValues) meanFlux += f;
    meanFlux /= fluxValues.size();
    
    double stdFlux = 0.0;
    for (double f : fluxValues) stdFlux += (f - meanFlux) * (f - meanFlux);
    stdFlux = std::sqrt(stdFlux / fluxValues.size());
    
    double threshold = meanFlux + 2.0 * stdFlux;
    double minInterval = 0.05;  // 50ms minimum
    int minFrames = static_cast<int>(minInterval * sampleRate / HOP_SIZE);
    
    int maxIdx = static_cast<int>(fluxValues.size()) - 1;
    int lastPeak = -minFrames;
    for (int i = 1; i < maxIdx; ++i)
    {
        if (fluxValues[i] > fluxValues[i-1] && fluxValues[i] > fluxValues[i+1]
            && fluxValues[i] > threshold
            && i - lastPeak >= minFrames)
        {
            double time = i * HOP_SIZE / sampleRate;
            result.transientTimes.push_back(time);
            lastPeak = i;
        }
    }
    
    return result;
}

bool HDAW::TransientDetector::startDetectFromFile(const juce::String& filePath, 
                                                    juce::AudioFormatManager& fm,
                                                    DetectCallback callback)
{
    // If a prior detection is still running, cancel it and wait.
    if (active.load())
    {
        cancelFlag = true;
        if (detectThread.joinable())
            detectThread.join();
        cancelFlag = false;
    }

    auto r = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(juce::File(filePath)));
    if (!r) return false;
    
    sourceFile = filePath;
    formatManager = &fm;
    reader = std::move(r);
    completionCallback = std::move(callback);
    active = true;
    
    detectThread = std::thread(&TransientDetector::detectThreadFunc, this);
    return true;
}

void HDAW::TransientDetector::detectThreadFunc()
{
    Result result;
    result.transientTimes.clear();
    
    // Load full file into buffer
    juce::AudioBuffer<float> buffer(reader->numChannels, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    
    if (!cancelFlag.load())
    {
        result = detectFromBuffer(buffer, reader->sampleRate);
    }
    
    active = false;
    
    if (completionCallback)
        completionCallback(result);
}

} // namespace HDAW