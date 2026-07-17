#include "FrontendTreeWatcher.h"
#include "FrontendServer.h"
#include "FrontendRpc.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"

#include <QTimer>

namespace frontend {

namespace {
constexpr int kDebounceMs = 16;   // coalesce burst edits into one broadcast
}

FrontendTreeWatcher::FrontendTreeWatcher(AudioEngine& engine, FrontendServer& server, QObject* parent)
    : QObject(parent), engine_(engine), server_(server)
{
    // Attach to the ROOT project tree, not a child node. See the header docs
    // and AGENTS.md "ValueTree listener orphans" — this is the only listener
    // attachment that survives File→New / project-load rebuilds.
    engine_.getProjectModel().getTree().addListener(this);

    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kDebounceMs);
    connect(debounceTimer_, &QTimer::timeout, this, [this]() {
        server_.broadcastNotification(notify::TreeChanged, QJsonObject{});
    });
}

FrontendTreeWatcher::~FrontendTreeWatcher() {
    engine_.getProjectModel().getTree().removeListener(this);
}

void FrontendTreeWatcher::scheduleNotify() {
    // The debounce timer is single-shot; repeated scheduleNotify() calls
    // during a burst just reset the timer so only one broadcast fires.
    if (!debounceTimer_->isActive())
        debounceTimer_->start();
}

} // namespace frontend
