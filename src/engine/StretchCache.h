#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "StretchRenderer.h"
#include <QObject>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace HDAW {

// Caches stretched renderings of audio clips so that
// `ClipSourceProcessor::prepareToPlay` (called during the synchronous
// `rebuildRoutingGraph`) doesn't re-render on every rebuild.
//
// Entries are keyed by (clipID, stretchRatio, sampleRate). The renderer
// runs on its own worker thread (see `StretchRenderer`); this cache only
// owns the renderer and stores results. The audio thread never touches
// this object — it reads the adopted buffer inside `ClipSourceProcessor`.
//
// `QObject` so the worker-thread `onComplete` callback can hop back to
// the message thread via `QMetaObject::invokeMethod(this, ...)` to fire
// `onReady`. `onReady` is invoked on the Qt message thread.
class StretchCache : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        int clipID = -1;
        double ratio = 1.0;
        double sampleRate = 0.0;
        juce::HeapBlock<int> data[2];
        int64_t length = 0;
        int channels = 0;
        bool ready = false;
    };

    StretchCache();
    ~StretchCache();

    // Returns a pointer to a ready entry matching (clipID, ratio, sampleRate),
    // or nullptr if none is ready. Safe to call from the message thread during
    // `prepareToPlay`. The returned pointer is stable until the entry is
    // invalidated by a new request with a different key for the same clip.
    const Entry* lookup(int clipID, double ratio, double sampleRate) const;

    // True iff a matching ready entry exists.
    bool has(int clipID, double ratio, double sampleRate) const;

    // Asynchronously render (clipID, ratio, sampleRate) if a matching ready
    // entry is not already cached. If a matching entry is already ready,
    // this is a no-op. `onReady` (the member signal) fires when a fresh
    // render completes; existing entries are NOT invalidated for callers
    // holding `lookup()` pointers mid-block, because replacement happens
    // only via `rebuildRoutingGraph()` (under graphLock).
    void requestRender(int clipID, const juce::String& sourceFile,
                       double ratio, double sampleRate,
                       juce::AudioFormatManager& formatManager,
                       QObject* progressReceiver = nullptr);

    // Drop the entry for `clipID` (e.g. when the clip is deleted). Safe no-op
    // if none exists. Other clips' entries are untouched.
    void invalidate(int clipID);

    // Drop all entries.
    void clear();

signals:
    // Emitted on the Qt message thread when a render finishes successfully.
    // Receivers should call `engine.getMainProcessor()->rebuildRoutingGraph()`
    // to swap the stretched buffer into the playing clip.
    void entryReady(int clipID);

private:
    // Holds the renderer's output and stores it into `entries_` from the
    // worker thread, then hops to the message thread to emit entryReady.
    void handleResult(int clipID, double ratio, double sampleRate,
                      const StretchRenderer::Result& result);

    mutable std::mutex entriesLock;
    std::vector<std::unique_ptr<Entry>> entries_;

    // Single renderer: only one stretch in flight at a time. Concurrent
    // requests for different clips while one is rendering are coalesced
    // (the most recent request wins). This is acceptable because renders
    // are background and brief; parallelism is a future optimization.
    StretchRenderer renderer_;
    std::mutex requestLock_;
    std::atomic<int> renderingClipID_{ -1 };
};

} // namespace HDAW
