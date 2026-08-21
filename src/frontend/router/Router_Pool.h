#pragma once
#include "../FrontendRpc.h"

class ReadModel;

namespace juce { class AudioFormatManager; }

namespace frontend {

DispatchResult dispatchPool(ReadModel& readModel, juce::AudioFormatManager& formatManager,
                            const QString& subMethod, const QJsonValue& params);

} // namespace frontend
