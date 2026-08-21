#include "Router_AudioGraph.h"
#include "RouterHelpers.h"

#include "../../common/AudioGraphCommands.h"

#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchAudioGraph(AudioGraphCommands& c, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "rebuildRoutingGraph")  { c.rebuildRoutingGraph();  return { false, QJsonValue::Null }; }
    if (m == "rebuildTrackFX")       { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildTrackFX(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildAutomationCache"){ int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildAutomationCache(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildModulation")    { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildModulation(i); return { false, QJsonValue::Null }; }
    if (m == "toggleFXEditor")       { int i, s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)) return makeError(-32602, "trackIndex and slotIndex required"); c.toggleFXEditor(i, s); return { false, QJsonValue::Null }; }
    if (m == "switchClipTake")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.switchClipTake(i); return { false, QJsonValue::Null }; }
    if (m == "switchClipTakeToIndex") { int cid, ti; if (!requireInt(o, "clipId", cid, nullptr) || !requireInt(o, "takeIndex", ti, nullptr)) return makeError(-32602, "clipId and takeIndex required"); c.switchClipTakeToIndex(cid, ti); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown audioGraph method: " + m);
}

} // namespace frontend
