#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace HDAW {

// Immutable decoded PCM. Built on the message thread; the audio thread only
// reads it. Held by shared_ptr so the refcount IS the consumer count.
struct DecodedSound
{
    std::unique_ptr<float[]> data[2] = { nullptr, nullptr }; // [0]=L, [1]=R (null when mono)
    int numChannels = 0;
    int64_t length = 0;          // per-channel sample count
    double sampleRate = 44100.0;

    // Message thread. Decodes `path` via `fm` into a fresh DecodedSound.
    // Returns nullptr when the file cannot be read (same contract as
    // AudioFormatManager::createReaderFor returning null).
    static std::shared_ptr<const DecodedSound> decode(juce::AudioFormatManager& fm,
                                                      const juce::String& path)
    {
        auto sound = std::make_shared<DecodedSound>();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(path)));
        if (r == nullptr)
            return nullptr;
        sound->numChannels = juce::jmin(static_cast<int>(r->numChannels), 2);
        sound->length = r->lengthInSamples;
        sound->sampleRate = r->sampleRate;
        if (sound->numChannels <= 0 || sound->length <= 0)
            return nullptr;
        const int total = static_cast<int>(sound->length);
        sound->data[0] = std::make_unique<float[]>(static_cast<size_t>(total));
        if (sound->numChannels > 1)
            sound->data[1] = std::make_unique<float[]>(static_cast<size_t>(total));
        float* const ptrs[2] = { sound->data[0].get(),
                                 sound->numChannels > 1 ? sound->data[1].get() : nullptr };
        r->read(ptrs, sound->numChannels, 0, total);
        return sound;
    }
};

// Message-thread-only decode cache. Keyed by full path; decodes once per file
// and hands out shared_ptr<const DecodedSound>. The map holds STRONG refs:
// an entry keeps its decode alive even with zero consumers, and the consumer
// count is observable as shared_ptr::use_count() (1 == pool only, >= 2 == at
// least one consumer). pruneUnreferenced() evicts entries whose use_count()
// is 1 — nothing outside the pool holds them. The audio thread never touches
// this class — consumers copy the pointer into their own member at prepare
// time, mirroring the SamplerSound pattern.
class DecodedSoundPool
{
public:
    explicit DecodedSoundPool(juce::AudioFormatManager& fm) : formatManager(fm) {}

    // Message thread. Returns the shared decode for `path`, decoding on first
    // request. A cache hit returns the existing entry directly — a strong map
    // never expires an entry, so the same sound is served whether or not other
    // consumers still hold it. Never decodes a file already in the cache
    // (decode-count is monotonic per genuinely-missed file). Returns nullptr
    // on unreadable file.
    std::shared_ptr<const DecodedSound> acquire(const juce::String& path)
    {
        const std::string key = juce::File(path).getFullPathName().toStdString();
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second; // strong ref — entry is always alive
        auto sound = DecodedSound::decode(formatManager, path);
        if (sound)
        {
            cache[key] = sound;
            decodeCount++;
        }
        return sound;
    }

    // Message thread. Drops cache entries no consumer references
    // (use_count()==1 means only the pool holds it). Called by tests and
    // opportunistically by acquire when the cache grows.
    void pruneUnreferenced()
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (it->second.use_count() <= 1)
                it = cache.erase(it);
            else
                ++it;
        }
    }

    // Test hooks.
    int getDecodeCount() const { return decodeCount; }
    int getEntryCount() const { return static_cast<int>(cache.size()); }

private:
    juce::AudioFormatManager& formatManager;
    std::unordered_map<std::string, std::shared_ptr<const DecodedSound>> cache;
    int decodeCount = 0;
};

} // namespace HDAW
