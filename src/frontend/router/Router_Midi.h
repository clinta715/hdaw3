#pragma once
#include "../FrontendRpc.h"

class MidiService;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchMidi(MidiService& s, const QString& subMethod,
                            const QJsonValue& params);
}
