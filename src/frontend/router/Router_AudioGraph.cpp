#include "Router_AudioGraph.h"
#include "RouterHelpers.h"

#include "../../common/AudioGraphCommands.h"

#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

// `trackList` is the project's TRACK_LIST node, passed in for the B2 stable-id
// track argument (`trackID` next to the positional `trackIndex`) — the ONE
// shared rule in common/StableRefResolve.h. AudioGraphCommands exposes only
// index-based operations, so FrontendRouter passes the tree in (dispatchRead /
// dispatchProject precedent). Everything else reads through `c`.
DispatchResult dispatchAudioGraph(AudioGraphCommands& c, const juce::ValueTree& trackList,
                                  const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "rebuildRoutingGraph")  { c.rebuildRoutingGraph();  return { false, QJsonValue::Null }; }
    if (m == "rebuildTrackFX")       { int i; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})) return err; c.rebuildTrackFX(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildAutomationCache"){ int i; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})) return err; c.rebuildAutomationCache(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildModulation")    { int i; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})) return err; c.rebuildModulation(i); return { false, QJsonValue::Null }; }
    if (m == "toggleFXEditor")       { int i, s; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err, HDAW::StableRefKeys{"trackIndex", "trackID"}) || !requireInt(o, "slotIndex", s, nullptr)) return err.isError ? err : makeError(-32602, "trackIndex and slotIndex required"); c.toggleFXEditor(i, s); return { false, QJsonValue::Null }; }
    if (m == "switchClipTake")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.switchClipTake(i); return { false, QJsonValue::Null }; }
    if (m == "switchClipTakeToIndex") { int cid, ti; if (!requireInt(o, "clipId", cid, nullptr) || !requireInt(o, "takeIndex", ti, nullptr)) return makeError(-32602, "clipId and takeIndex required"); c.switchClipTakeToIndex(cid, ti); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown audioGraph method: " + m);
}

} // namespace frontend
