#pragma once
#include "../FrontendRpc.h"

class ReadModel;
class QJsonValue;
class QString;

namespace juce { class ValueTree; }

namespace frontend {
// `trackList` is the project's TRACK_LIST node and `busList` its
// ROUTING_GRAPH/BUS_LIST node. Neither is part of the ReadModel facade, so
// FrontendRouter passes the trees in: `trackList` for the B2 stable-id argument
// read.getTrackSends accepts (`trackID` next to the positional `trackId`), and
// `busList` for the two bus reads (read.listBuses / read.listBusFxParams) — the
// same reason read.getWaveformPeaks is special-cased there. Everything else
// reads through `r`.
DispatchResult dispatchRead(ReadModel& r, const juce::ValueTree& trackList,
                            const juce::ValueTree& busList,
                            const QString& subMethod, const QJsonValue& params);
}
