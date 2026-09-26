#pragma once
#include "../FrontendRpc.h"

class AudioGraphCommands;
class QJsonValue;
class QString;

namespace juce { class ValueTree; }

namespace frontend {
// `trackList` is the project's TRACK_LIST node: the B2 stable-id track
// argument (`trackID` next to the positional `trackIndex`) resolves against
// it (dispatchRead precedent — the commands interface has no tree access).
DispatchResult dispatchAudioGraph(AudioGraphCommands& cmds, const juce::ValueTree& trackList,
                                   const QString& subMethod, const QJsonValue& params);
}
