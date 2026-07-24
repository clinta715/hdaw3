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
};

} // namespace frontend
