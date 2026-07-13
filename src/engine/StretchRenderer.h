#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>
#include <functional>

namespace HDAW {

// Pitch-preserving time-stretch renderer.
//
// Decodes a source audio file and produces a stretched version of it into
// two `juce::HeapBlock<int>` buffers (matching `ClipSourceProcessor::preloadedData`'s
// 16-bit-ish int PCM format — values in roughly [-32768, 32767], converted
// to float by dividing by 32768.0f on the read side).
//
// The render runs on a worker thread (never the audio thread); the backend
// is SoundTouch (LGPL-2.1), but the rest of the engine only sees this
// interface so the backend can be swapped without touching callers.
//
// Shape mirrors `ExportManager`: `std::thread` + two atomic flags +
// `std::function` callbacks invoked on the worker thread. Callers are
// responsible for hopping to the UI thread (e.g. `QMetaObject::invokeMethod`).
class StretchRenderer
{
public:
    // Result delivered to `onComplete`. On `success`, `data[0]`/`data[1]`
    // hold the stretched stereo channels (mono duplicated to both), and
    // `length` is the per-channel sample count. On failure the blocks are
    // freed and `length == 0`.
    struct Result
    {
        bool success = false;
        juce::HeapBlock<int> data[2];
        int64_t length = 0;
        int channels = 0;
        double sampleRate = 0.0;
    };

    StretchRenderer();
    ~StretchRenderer();

    StretchRenderer(const StretchRenderer&) = delete;
    StretchRenderer& operator=(const StretchRenderer&) = delete;

    // Begins a render. Returns false if a render is already in flight.
    // `timeRatio` is the time stretch factor: >1 slows down (longer),
    // <1 speeds up (shorter). Pitch is preserved regardless of ratio.
    // The reader used for decoding is taken from `formatManager`.
    bool startRender(const juce::String& sourceFile, double timeRatio,
                     double sampleRate, juce::AudioFormatManager& formatManager);

    // Cooperative cancel; the worker polls the flag per processing block.
    void cancel();
    bool isRendering() const { return active.load(); }

    // Invoked from the worker thread with progress in [0,1] during decode+process.
    std::function<void(float)> onProgress;
    // Invoked from the worker thread exactly once when the render ends.
    // The receiver owns the Result's buffers. May be empty.
    std::function<void(const Result&)> onComplete;

private:
    void renderThreadFunc(juce::String sourceFile, double timeRatio,
                          double sampleRate, juce::AudioFormatManager* formatManager);

    std::atomic<bool> active{ false };
    std::atomic<bool> cancelFlag{ false };
    std::thread renderThread;
};

} // namespace HDAW
