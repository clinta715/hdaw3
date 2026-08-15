#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace HDAW {

// One open AudioFormatReader per file, shared by every streaming clip that
// references it. Metadata is read once at open; reads go through readLock so
// concurrent consumers never interleave seeks on the shared reader.
struct StreamingSoundHandle
{
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::CriticalSection readLock;
    int numChannels = 0;
    int64_t lengthInSamples = 0;
    double sampleRate = 44100.0;

    // Message thread. Opens `file` via `fm` into a fresh handle. Returns
    // nullptr when the file cannot be read (same contract as
    // AudioFormatManager::createReaderFor returning null).
    static std::shared_ptr<StreamingSoundHandle> open(juce::AudioFormatManager& fm,
                                                      const juce::File& file)
    {
        auto handle = std::make_shared<StreamingSoundHandle>();
        handle->reader.reset(fm.createReaderFor(file));
        if (handle->reader == nullptr)
            return nullptr;
        handle->numChannels = juce::jmin(static_cast<int>(handle->reader->numChannels), 2);
        handle->lengthInSamples = handle->reader->lengthInSamples;
        handle->sampleRate = handle->reader->sampleRate;
        return handle;
    }

    // Background reader threads, the export render thread, and message-thread
    // head fills — never the realtime audio callback. Serialized by readLock.
    void read(float* const* destChannels, int numDestChannels,
              int64_t startSampleInSource, int numSamplesToRead)
    {
        const juce::ScopedLock lock(readLock);
        if (reader)
            reader->read(destChannels, numDestChannels, startSampleInSource,
                         numSamplesToRead);
    }
};

// Message-thread-only reader cache. Keyed by full path; opens one reader per
// file and hands out shared_ptr<StreamingSoundHandle>. The map holds STRONG
// refs: an entry keeps its reader alive even with zero consumers, and the
// consumer count is observable as shared_ptr::use_count() (1 == pool only,
// >= 2 == at least one consumer). pruneUnreferenced() evicts entries whose
// use_count() is 1 — nothing outside the pool holds them. The realtime audio
// callback never touches this class — consumers copy the pointer into their
// own member at prepare time, mirroring the SamplerSound pattern.
//
// Threading: acquire/pruneUnreferenced have the same threading profile as
// DecodedSoundPool — normally the message thread, with the same serialized
// exceptions (prebuildTracks pre-park acquire, device-restart prepareToPlay
// always hitting the strong cache, strictly safer than the inline reader
// open it replaced). The handle's readLock is held by background reader
// threads, the export render thread, and message-thread head fills — never
// by the realtime audio callback.
class StreamingSoundPool
{
public:
    explicit StreamingSoundPool(juce::AudioFormatManager& fm) : formatManager(fm) {}

    // Message thread. Returns the shared reader for `path`, opening on first
    // request. A cache hit returns the existing entry directly — a strong map
    // never expires an entry. Never opens a file already in the cache
    // (open-count is monotonic per genuinely-missed file). Returns nullptr
    // on unreadable file.
    std::shared_ptr<StreamingSoundHandle> acquire(const juce::String& path)
    {
        const std::string key = juce::File(path).getFullPathName().toStdString();
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second; // strong ref — entry is always alive
        auto handle = StreamingSoundHandle::open(formatManager, juce::File(path));
        if (handle)
        {
            cache[key] = handle;
            openCount++;
        }
        return handle;
    }

    // Message thread. Drops cache entries no consumer references
    // (use_count()==1 means only the pool holds it).
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
    int getOpenCount() const { return openCount; }
    int getEntryCount() const { return static_cast<int>(cache.size()); }

private:
    juce::AudioFormatManager& formatManager;
    std::unordered_map<std::string, std::shared_ptr<StreamingSoundHandle>> cache;
    int openCount = 0;
};

} // namespace HDAW
