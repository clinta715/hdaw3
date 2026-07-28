#include "FrontendTreeWatcher.h"
#include "FrontendServer.h"
#include "FrontendRpc.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"

#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

#include <cstdlib>

namespace frontend {

namespace {
constexpr int kDebounceMs = 16;   // coalesce burst edits into one broadcast
}

FrontendTreeWatcher::FrontendTreeWatcher(AudioEngine& engine, FrontendServer& server, QObject* parent)
    : QObject(parent), engine_(engine), server_(server)
{
    // Kill-switch: HDAW_FORCE_FULL_SYNC=1 forces a full snapshot re-fetch for
    // every change (disables the delta path). Mirrors the HDAW_WATCH_PLUGINS
    // parsing in FrontendServer; default is off (delta path active).
    forceFullSync_ = []() {
        const auto* v = std::getenv("HDAW_FORCE_FULL_SYNC");
        if (v == nullptr) return false;
        return v[0] != '0' && v[0] != 'f' && v[0] != 'F';
    }();

    // Attach to the ROOT project tree (documented-safe pattern; survives
    // File->New / load rebuilds). See AGENTS.md "ValueTree listener orphans".
    engine_.getProjectModel().getTree().addListener(this);
    accumulator_.setBpm(engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0));

    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kDebounceMs);
    connect(debounceTimer_, &QTimer::timeout, this, [this]() { flush(); });
}

FrontendTreeWatcher::~FrontendTreeWatcher() {
    engine_.getProjectModel().getTree().removeListener(this);
}

void FrontendTreeWatcher::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) {
    if (tree.hasType(IDs::PROJECT) && property == IDs::tempo)
        accumulator_.setBpm(tree.getProperty(IDs::tempo, 120.0));
    accumulator_.notePropertyChanged(tree, property);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) {
    accumulator_.noteChildAdded(child);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) {
    accumulator_.noteChildRemoved(child);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildOrderChanged(juce::ValueTree&, int, int) {
    accumulator_.noteStructuralChange();
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeParentChanged(juce::ValueTree&) {
    accumulator_.noteStructuralChange();
    scheduleNotify();
}

void FrontendTreeWatcher::scheduleNotify() {
    // Single-shot debounce: repeated calls during a burst just keep it pending.
    if (!debounceTimer_->isActive())
        debounceTimer_->start();
}

void FrontendTreeWatcher::flush() {
    // A fullSync is broadcast when the accumulator escalated, or when the
    // kill-switch is armed and there is any snapshot-relevant change (routing
    // what would have been a delta to a full re-fetch instead).
    if (accumulator_.fullSync() || (forceFullSync_ && !accumulator_.empty())) {
        server_.broadcastNotification(notify::TreeChanged,
            QJsonObject{ { "fullSync", true } });
    } else if (!accumulator_.empty()) {
        QJsonObject payload{ { "fullSync", false } };

        QJsonArray clipsUpserted;
        for (const auto& [id, c] : accumulator_.clipsUpserted())
            clipsUpserted.append(toJson(c));
        payload.insert("clipsUpserted", clipsUpserted);

        QJsonArray clipsRemoved;
        for (int id : accumulator_.clipsRemoved())
            clipsRemoved.append(id);
        payload.insert("clipsRemoved", clipsRemoved);

        QJsonArray tracksUpserted;
        for (const auto& [id, t] : accumulator_.tracksUpserted())
            tracksUpserted.append(toJson(t));
        payload.insert("tracksUpserted", tracksUpserted);

        server_.broadcastNotification(notify::TreeChanged, payload);
    }
    // else: nothing snapshot-relevant changed -> no broadcast.
    accumulator_.reset();
}

} // namespace frontend
