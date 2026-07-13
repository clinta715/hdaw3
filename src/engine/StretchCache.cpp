#include "StretchCache.h"
#include <QMetaObject>
#include <algorithm>

namespace HDAW {

namespace {
bool keyMatches(const StretchCache::Entry& e, int clipID, double ratio, double sampleRate)
{
    return e.clipID == clipID
        && std::abs(e.ratio - ratio) < 1e-4
        && std::abs(e.sampleRate - sampleRate) < 0.5;
}
} // namespace

StretchCache::StretchCache() = default;

StretchCache::~StretchCache()
{
    // renderer_'s destructor joins its worker thread.
}

const StretchCache::Entry* StretchCache::lookup(int clipID, double ratio, double sampleRate) const
{
    std::lock_guard<std::mutex> lk(entriesLock);
    for (const auto& e : entries_)
        if (e->ready && keyMatches(*e, clipID, ratio, sampleRate))
            return e.get();
    return nullptr;
}

bool StretchCache::has(int clipID, double ratio, double sampleRate) const
{
    return lookup(clipID, ratio, sampleRate) != nullptr;
}

void StretchCache::requestRender(int clipID, const juce::String& sourceFile,
                                 double ratio, double sampleRate,
                                 juce::AudioFormatManager& formatManager,
                                 QObject* progressReceiver)
{
    // Fast path: a matching ready entry already exists.
    if (has(clipID, ratio, sampleRate))
        return;

    // Coalesce: only one render at a time; drop overlapping requests.
    std::lock_guard<std::mutex> lk(requestLock_);
    if (renderer_.isRendering())
        return; // an in-flight render will be picked up by a later rebuild

    renderingClipID_.store(clipID, std::memory_order_relaxed);

    // Capture by value; lifetime: renderer_ outlives this call.
    renderer_.onComplete = [this, clipID, ratio, sampleRate](const StretchRenderer::Result& result)
    {
        handleResult(clipID, ratio, sampleRate, result);
    };

    renderer_.startRender(sourceFile, ratio, sampleRate, formatManager);
}

void StretchCache::handleResult(int clipID, double ratio, double sampleRate,
                                const StretchRenderer::Result& result)
{
    // Called on the renderer's worker thread.
    if (!result.success || result.length <= 0)
    {
        renderingClipID_.store(-1, std::memory_order_relaxed);
        return;
    }

    // Copy the result's buffers into a fresh entry. The renderer is done
    // with them (onComplete fires exactly once at end-of-render), so a
    // straightforward copy is safe; the source blocks are freed when the
    // result's temporaries are destroyed after onComplete returns.
    auto entry = std::make_unique<Entry>();
    entry->clipID = clipID;
    entry->ratio = ratio;
    entry->sampleRate = sampleRate;
    entry->length = result.length;
    entry->channels = result.channels;
    entry->data[0].malloc(static_cast<size_t>(result.length));
    entry->data[1].malloc(static_cast<size_t>(result.length));
    std::copy_n(result.data[0].get(), static_cast<size_t>(result.length), entry->data[0].get());
    std::copy_n(result.data[1].get(), static_cast<size_t>(result.length), entry->data[1].get());
    entry->ready = true;

    {
        std::lock_guard<std::mutex> lk(entriesLock);
        // Replace any prior entry for the same clipID to avoid unbounded growth.
        for (auto& existing : entries_)
        {
            if (existing->clipID == clipID)
            {
                existing = std::move(entry);
                entry.reset(); // sentinel: don't push below
                break;
            }
        }
        if (entry)
            entries_.push_back(std::move(entry));
    }

    renderingClipID_.store(-1, std::memory_order_relaxed);

    // Hop to the message thread to emit entryReady, so receivers can safely
    // touch the project model / call rebuildRoutingGraph().
    QMetaObject::invokeMethod(this, [this, clipID]()
    {
        emit entryReady(clipID);
    }, Qt::QueuedConnection);
}

void StretchCache::invalidate(int clipID)
{
    std::lock_guard<std::mutex> lk(entriesLock);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [clipID](const std::unique_ptr<Entry>& e) { return e->clipID == clipID; }),
        entries_.end());
}

void StretchCache::clear()
{
    std::lock_guard<std::mutex> lk(entriesLock);
    entries_.clear();
}

} // namespace HDAW
