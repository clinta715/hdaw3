#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ProjectPool.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/Dx7SysexImport.h"
#include "../engine/MidiFx.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>

namespace mcp {

static std::optional<HDAW::EnvelopeGenerator::Shape> parseEnvelopeShape(const QString& s) {
    using S = HDAW::EnvelopeGenerator::Shape;
    if (s == "ramp") return S::Ramp;
    if (s == "adsr") return S::ADSR;
    if (s == "sine") return S::Sine;
    if (s == "triangle") return S::Triangle;
    if (s == "saw") return S::Saw;
    if (s == "square") return S::Square;
    if (s == "pulse") return S::Pulse;
    if (s == "staircase") return S::Staircase;
    if (s == "sCurve") return S::SCurve;
    if (s == "randomWalk") return S::RandomWalk;
    if (s == "noise") return S::Noise;
    return std::nullopt;
}


void registerEnvelopeTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_envelope_shapes",
        "List all available envelope generator shapes.",
        objSchema({}),
        "envelope",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto addShape = [&](const char* name, const char* desc) {
                arr.append(QJsonObject{{"name", name}, {"description", desc}});
            };
            addShape("ramp", "Linear ramp between two values");
            addShape("adsr", "Attack-Decay-Sustain-Release envelope");
            addShape("sine", "Sine wave LFO");
            addShape("triangle", "Triangle wave LFO");
            addShape("saw", "Sawtooth wave LFO");
            addShape("square", "Square wave LFO");
            addShape("pulse", "Pulse wave (50% duty)");
            addShape("staircase", "Stepped quantization");
            addShape("sCurve", "S-curve (smooth step)");
            addShape("randomWalk", "Seeded random walk");
            addShape("noise", "Seeded uniform noise");
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"shapes", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"generate_automation_envelope",
        "Generate an envelope shape on an automation lane.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                   {"lane",     QJsonObject{{"oneOf", QJsonArray{
                       QJsonObject{{"type","integer"}},
                       QJsonObject{{"type","string"}}}}}},
                   {"shape",    QJsonObject{{"type","string"}}},
                   {"start",    QJsonObject{{"type","number"}}},
                   {"end",      QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue", QJsonObject{{"type","number"}}},
                   {"cycles",   QJsonObject{{"type","number"}}},
                   {"steps",    QJsonObject{{"type","integer"}}},
                   {"phase",    QJsonObject{{"type","number"}}},
                   {"density",  QJsonObject{{"type","number"}}},
                   {"smooth",   QJsonObject{{"type","number"}}},
                   {"seed",     QJsonObject{{"type","integer"}}}},
                  {"trackId","lane","shape"}),
        "envelope",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto laneRef = a.value("lane");
            auto lane = findLane(e, trackId, laneRef);
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);
            std::string laneName = lane.getProperty(IDs::name, "").toString().toStdString();

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(1.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateAutomationEnvelope(trackId, laneName, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_gain_envelope",
        "Generate an envelope shape on a clip's gain envelope.",
        objSchema({{"clipId",     QJsonObject{{"type","integer"}}},
                   {"shape",      QJsonObject{{"type","string"}}},
                   {"start",      QJsonObject{{"type","number"}}},
                   {"end",        QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue",   QJsonObject{{"type","number"}}},
                   {"cycles",     QJsonObject{{"type","number"}}},
                   {"steps",      QJsonObject{{"type","integer"}}},
                   {"phase",      QJsonObject{{"type","number"}}},
                   {"density",    QJsonObject{{"type","number"}}},
                   {"smooth",     QJsonObject{{"type","number"}}},
                   {"seed",       QJsonObject{{"type","integer"}}}},
                  {"clipId","shape"}),
        "envelope",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(2.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipGainEnvelope(clipId, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_cc_lane",
        "Generate an envelope shape on a MIDI clip's CC lane.",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                   {"controllerNumber", QJsonObject{{"type","integer"}}},
                   {"shape",            QJsonObject{{"type","string"}}},
                   {"start",            QJsonObject{{"type","number"}}},
                   {"end",              QJsonObject{{"type","number"}}},
                   {"startValue",       QJsonObject{{"type","number"}}},
                   {"endValue",         QJsonObject{{"type","number"}}},
                   {"cycles",           QJsonObject{{"type","number"}}},
                   {"steps",            QJsonObject{{"type","integer"}}},
                   {"phase",            QJsonObject{{"type","number"}}},
                   {"density",          QJsonObject{{"type","number"}}},
                   {"smooth",           QJsonObject{{"type","number"}}},
                   {"seed",             QJsonObject{{"type","integer"}}}},
                  {"clipId","controllerNumber","shape"}),
        "envelope",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            int controllerNumber = a.value("controllerNumber").toInt(-1);
            if (controllerNumber < 0 || controllerNumber > 127)
                return McpToolResult::text("controllerNumber must be 0-127", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(127.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipCcLane(clipId, controllerNumber, params);
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
