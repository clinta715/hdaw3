#pragma once
#include <memory>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace HDAW {

// Immutable loaded-sample resource. Built on the message thread; the audio
// thread only reads it. Once built it is never mutated (slice edits build a
// new SamplerSound). Held by shared_ptr; voices capture a raw pointer that is
// valid for the voice's lifetime via the engine's block-boundary swap.
struct SamplerSound
{
    const float* data[2] = { nullptr, nullptr };   // non-owning view onto owned storage
    int numChannels = 0;
    int64_t length = 0;            // per-channel sample count
    double nativeSampleRate = 44100.0;
    int rootNote = 60;

    // Normalized 0..1 sample-internal coords (lesson 1: never timeline beats).
    double sampleStart = 0.0, sampleEnd = 1.0;
    double loopStart = 0.0, loopEnd = 1.0;
    bool loopEnabled = false;

    std::vector<int64_t> slicePoints; // frame indices (boundaries), sorted

    int64_t startFrame() const noexcept { return static_cast<int64_t>(sampleStart * length); }
    int64_t endFrame()   const noexcept { return static_cast<int64_t>(sampleEnd   * length); }
    int64_t loopStartFrame() const noexcept { return static_cast<int64_t>(loopStart * length); }
    int64_t loopEndFrame()   const noexcept { return static_cast<int64_t>(loopEnd   * length); }

    struct Builder
    {
        int numChannels = 0;
        int64_t length = 0;
        double nativeSampleRate = 44100.0;
        int rootNote = 60;
        double sampleStart = 0.0, sampleEnd = 1.0;
        double loopStart = 0.0, loopEnd = 1.0;
        bool loopEnabled = false;
        std::unique_ptr<float[]> data[2];
        std::vector<int64_t> slicePoints;

        std::shared_ptr<const SamplerSound> build()
        {
            auto* s = new SamplerSound();
            s->numChannels = numChannels;
            s->length = length;
            s->nativeSampleRate = nativeSampleRate;
            s->rootNote = rootNote;
            s->sampleStart = sampleStart; s->sampleEnd = sampleEnd;
            s->loopStart = loopStart;     s->loopEnd = loopEnd;
            s->loopEnabled = loopEnabled;
            s->slicePoints = std::move(slicePoints);
            auto storage = std::make_shared<Owned>();
            storage->owned[0] = std::move(data[0]);
            storage->owned[1] = std::move(data[1]);
            s->data[0] = storage->owned[0].get();
            s->data[1] = storage->owned[1].get();
            // Alias-shared ptr: refcounts `storage`, points to `s`.
            return std::shared_ptr<const SamplerSound>(storage, s);
        }

    private:
        struct Owned { std::unique_ptr<float[]> owned[2]; };
    };
};

} // namespace HDAW
