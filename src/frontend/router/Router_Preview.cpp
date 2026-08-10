#include "Router_Preview.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"

#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchPreview(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& preview = engine.getPreviewPlayer();

    if (m == "load") {
        std::string path;
        if (!requireString(o, "filePath", path, nullptr))
            return makeError(-32602, "filePath required");
        preview.loadFile(juce::File(juce::String(path)));
        return { false, QJsonValue::Null };
    }
    if (m == "play") {
        preview.play();
        return { false, QJsonValue::Null };
    }
    if (m == "stop") {
        preview.stop();
        return { false, QJsonValue::Null };
    }
    if (m == "setVolume") {
        float vol;
        if (!requireFloat(o, "volume", vol, nullptr))
            return makeError(-32602, "volume required");
        preview.setVolume(vol);
        return { false, QJsonValue::Null };
    }
    if (m == "setTempoMatch") {
        bool enabled;
        double fileBpm = optDouble(o, "fileBpm", 0.0, nullptr);
        if (!requireBool(o, "enabled", enabled, nullptr))
            return makeError(-32602, "enabled required");
        preview.setTempoMatch(enabled, fileBpm);
        return { false, QJsonValue::Null };
    }
    if (m == "setProjectBpm") {
        double bpm;
        if (!requireDouble(o, "bpm", bpm, nullptr))
            return makeError(-32602, "bpm required");
        preview.setProjectBpm(bpm);
        return { false, QJsonValue::Null };
    }
    if (m == "isPlaying") {
        return { false, preview.isPlaying() };
    }
    return makeError(-32601, "unknown preview method: " + m);
}

} // namespace frontend
