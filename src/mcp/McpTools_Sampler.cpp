#include "McpTools.h"
#include "McpTools_Private.h"
// B2: the stable-id argument helpers (`trackId`/`trackID`) — thin readers over
// the ONE shared rule in common/StableRefResolve.h, whose error text is what
// the RPC twin reports for the same request.
#include "McpArgs.h"
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
#include "../common/SamplerStateJson.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>

namespace mcp {

void registerSamplerTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"sampler_set_sample",
        "Load an audio file into a sampler FX slot. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"filePath",  QJsonObject{{"type","string"}}},
                  {"rootNote",  QJsonObject{{"type","integer"}}}},
                  {"slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);
            juce::File file(filePath.toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            int root = a.value("rootNote").toInt(60);
            e->getProjectCommands().setSamplerSample(ti, si, filePath.toStdString(), root);
            return McpToolResult::text("ok");
        }});

s.registerTool({"sampler_get_state",
        "Get the current state of a sampler FX slot. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                  {"slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            // B3: the shared payload shaper (common/SamplerStateJson.h) reads
            // the ReadModel snapshot — hasSound is the LIVE decoded-sound
            // check (SamplerEngine::currentSound()), hasSampleFile the
            // property-only signal. The old property-only read masked
            // staged-but-silent slots (lesson 33) and had drifted from the
            // RPC payloads.
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(HDAW::samplerStateJson(
                                  e->getReadModel().getSamplerState(ti, si)))
                    .toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"set_sampler_param",
        "Set a sampler FX slot parameter. Either a named slot property ({property, value}: mono, playReverse, transpose, baseNote) or a real parameter value by paramIndex. Unknown property names are an ERROR — keyRange has its own tool (set_sampler_key_range). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",    QJsonObject{{"type","integer"}}},
                  {"trackID",    QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}},
                  {"paramIndex", QJsonObject{{"type","integer"}}},
                  {"property",   QJsonObject{{"type","string"}}},
                  {"value",      QJsonObject{{"type","number"}}}},
                  {"slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            if (a.contains("property"))
            {
                // B12 (Modular Dawn audit): set_sampler_param silently no-oped on
                // unknown properties (e.g. param:'keyRange' — the real tool is
                // set_sampler_key_range) while reporting ok, so agents believed
                // a write had landed that never did. Validate against the exact
                // set setSamplerProperty implements; anything else is an error.
                static const char* kSamplerProperties[] = { "mono", "playReverse", "transpose", "baseNote" };
                QString prop = a.value("property").toString();
                bool known = false;
                for (const char* k : kSamplerProperties)
                    if (prop == k) { known = true; break; }
                if (!known)
                    return McpToolResult::text(
                        QString("unknown sampler property '%1' (valid: mono, playReverse, transpose, baseNote; keyRange is set via set_sampler_key_range)")
                            .arg(prop), true);
                // The wire value may arrive as a JSON bool (mono/playReverse)
                // or a number (transpose/baseNote) — QJsonValue's scalar
                // readers return the DEFAULT for the wrong type, so normalize
                // explicitly (B12 family: silent value coercion).
                const QJsonValue wireVal = a.value("value");
                e->getAudioEngineCommands().setSamplerProperty(
                    ti, si, prop.toStdString(),
                    wireVal.isBool() ? (wireVal.toBool() ? 1.0 : 0.0)
                                     : wireVal.toDouble());
                return McpToolResult::text(QJsonDocument(QJsonObject{{"ok", true}})
                    .toJson(QJsonDocument::Compact));
            }
            if (!a.contains("paramIndex"))
                return McpToolResult::text(
                    "set_sampler_param requires either 'property' or 'paramIndex' (with 'value')", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_sampler_mode",
        "Set a sampler FX slot's playback mode. mode in {classic, one-shot, slice}. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"mode",      QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"classic","one-shot","slice"}}}}},
                  {"slotIndex","mode"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            QString mode = a.value("mode").toString();
            e->getAudioEngineCommands().setSamplerMode(ti, si, mode.toStdString());
            return McpToolResult::text(QJsonDocument(QJsonObject{{"ok", true}})
                .toJson(QJsonDocument::Compact));
        }});

s.registerTool({"detect_sampler_slices",
        "Detect slice points for a sampler FX slot (transient or grid mode) and store them. Returns {ok, totalSlices, slicePoints} (normalized 0..1). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",          QJsonObject{{"type","integer"}}},
                  {"trackID",         QJsonObject{{"type","integer"}}},
                  {"slotIndex",        QJsonObject{{"type","integer"}}},
                  {"sliceMode",        QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"transient","grid"}}}},
                  {"sliceGrid",        QJsonObject{{"type","number"}}},
                  {"sliceSensitivity", QJsonObject{{"type","number"}}}},
                  {"slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            std::string sm = a.value("sliceMode").toString("transient").toStdString();
            double grid = a.value("sliceGrid").toDouble(0.25);
            double sens = a.value("sliceSensitivity").toDouble(0.5);
            auto r = e->getAudioEngineCommands().detectSamplerSlices(ti, si, sm, grid, sens);
            QJsonArray pts;
            for (float p : r.slicePoints)
                pts.append(static_cast<double>(p));
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"ok", r.ok},
                            {"totalSlices", r.totalSlices},
                            {"slicePoints", pts}}).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"trigger_sampler_slice",
        "Audition one slice of a sampler FX slot in slice mode. Returns {ok, totalSlices}. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",    QJsonObject{{"type","integer"}}},
                  {"trackID",    QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}},
                  {"sliceIndex", QJsonObject{{"type","integer"}}},
                  {"velocity",   QJsonObject{{"type","number"}}}},
                  {"slotIndex","sliceIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            int idx = a.value("sliceIndex").toInt();
            float vel = static_cast<float>(a.value("velocity").toDouble(0.8));
            auto r = e->getAudioEngineCommands().triggerSamplerSlice(ti, si, idx, vel);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"ok", r.ok},
                            {"totalSlices", r.totalSlices}}).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"set_sampler_key_range",
        "Set the MIDI note range for a sampler FX slot. When a key range is set (keyLow and keyHigh are 0..127), only MIDI notes in that range are rendered by this sampler; notes outside the range pass to the next slot. Set both to -1 to restore full-range (default behavior). Enables multiple samplers on one track to each handle different note ranges (e.g. riser + downlifter). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"keyLow",    QJsonObject{{"type","integer"},{"minimum",-1},{"maximum",127}}},
                  {"keyHigh",   QJsonObject{{"type","integer"},{"minimum",-1},{"maximum",127}}}},
                  {"slotIndex","keyLow","keyHigh"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            int keyLow = a.value("keyLow").toInt(-1);
            int keyHigh = a.value("keyHigh").toInt(-1);
            e->getAudioEngineCommands().setSamplerKeyRange(ti, si, keyLow, keyHigh);
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(QJsonObject{
                    {"ok", true},
                    {"keyRangeLow", keyLow},
                    {"keyRangeHigh", keyHigh}}).toJson(QJsonDocument::Compact)));
        }});

}

} // namespace mcp
