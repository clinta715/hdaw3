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

        // Defined out-of-line below: build() uses SamplerSoundStorage, which
        // embeds SamplerSound by value and so requires SamplerSound to be a
        // complete type (not possible while Owned-style types are nested
        // inside SamplerSound's own body).
        std::shared_ptr<const SamplerSound> build();
    };
};

namespace detail {
// Single bundle so ONE shared_ptr control block governs both the float storage
// and the SamplerSound wrapper. Embedding SamplerSound by value requires the
// type to be complete, hence this lives at namespace scope (not nested inside
// SamplerSound). On refcount -> 0 the control block deletes this object,
// destroying `sound` (incl. its slicePoints vector) and the two buffers
// together. Nothing is leaked; the struct is never separately `new`ed.
struct SamplerSoundStorage
{
    std::unique_ptr<float[]> owned[2];
    SamplerSound sound;
};
} // namespace detail

inline std::shared_ptr<const SamplerSound> SamplerSound::Builder::build()
{
    auto storage = std::make_shared<detail::SamplerSoundStorage>();
    storage->sound.numChannels = numChannels;
    storage->sound.length = length;
    storage->sound.nativeSampleRate = nativeSampleRate;
    storage->sound.rootNote = rootNote;
    storage->sound.sampleStart = sampleStart; storage->sound.sampleEnd = sampleEnd;
    storage->sound.loopStart = loopStart;     storage->sound.loopEnd = loopEnd;
    storage->sound.loopEnabled = loopEnabled;
    storage->sound.slicePoints = std::move(slicePoints);
    storage->owned[0] = std::move(data[0]);
    storage->owned[1] = std::move(data[1]);
    storage->sound.data[0] = storage->owned[0].get();
    storage->sound.data[1] = storage->owned[1].get();
    // Alias-shared ptr: refcounts `storage`, points to the embedded `sound`.
    // `sound` lives inside the SamplerSoundStorage object the control block
    // owns and deletes, so it is destroyed together with the buffers.
    return std::shared_ptr<const SamplerSound>(storage, &storage->sound);
}

} // namespace HDAW
