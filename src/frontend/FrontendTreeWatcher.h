#pragma once

#include <QObject>
#include <juce_data_structures/juce_data_structures.h>
#include "TreeDeltaAccumulator.h"

class AudioEngine;

namespace frontend {

class FrontendServer;

// Bridges juce::ValueTree mutations on the project root tree to
// notify.treeChanged broadcasts to all connected WebSocket clients.
//
// Attaches to engine.getProjectModel().getTree() (the *root* tree — the
// documented-safe pattern from AGENTS.md "ValueTree listener orphans": a
// listener on a child node is orphaned when the tree is rebuilt by
// File→New / load; the root listener survives). The same pattern AudioEngine
// itself uses.
//
// A debounce timer (~16 ms) coalesces burst edits (e.g. a multi-property
// clip drag) into a single broadcast so the client re-fetches the snapshot
// once per burst instead of once per property write.
//
// Kill-switch: set HDAW_FORCE_FULL_SYNC=1 to route every change to a full
// snapshot re-fetch instead of an incremental delta (disables the delta path
// in the field if a drift bug ever surfaces). See forceFullSync_.
class FrontendTreeWatcher : public QObject,
                            private juce::ValueTree::Listener {
    Q_OBJECT
public:
    FrontendTreeWatcher(AudioEngine& engine, FrontendServer& server, QObject* parent = nullptr);
    ~FrontendTreeWatcher() override;

private:
    // juce::ValueTree::Listener — feed the accumulator, then schedule a flush.
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;
    void valueTreeParentChanged(juce::ValueTree&) override;

    void scheduleNotify();
    void flush();

    AudioEngine& engine_;
    FrontendServer& server_;
    class QTimer* debounceTimer_ = nullptr;
    TreeDeltaAccumulator accumulator_;

    // Kill-switch (HDAW_FORCE_FULL_SYNC): when armed, every snapshot-relevant
    // change broadcasts a full re-fetch instead of an incremental delta, so the
    // delta path can be disabled in the field if a drift bug ever surfaces.
    // Read once at construction; default false (delta path active).
    bool forceFullSync_ = false;
};

} // namespace frontend
