#pragma once
#include "../FrontendRpc.h"

class ReadModel;
class QJsonValue;
class QString;

namespace juce { class ValueTree; }

namespace frontend {
// `busList` is the project's ROUTING_GRAPH/BUS_LIST node. The bus surface is not part
// of the ReadModel facade, so FrontendRouter passes the tree in for the two bus reads
// (read.listBuses / read.listBusFxParams) — the same reason read.getWaveformPeaks is
// special-cased there. Everything else reads through `r`.
DispatchResult dispatchRead(ReadModel& r, const juce::ValueTree& busList,
                            const QString& subMethod, const QJsonValue& params);
}
