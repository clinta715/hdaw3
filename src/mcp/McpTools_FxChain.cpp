// ChainLibrary.h MUST stay the first include: Qt defines a `slots` macro
// (qobjectdefs.h, via McpServer.h/QObject below) that would otherwise rewrite
// the HDAW::ChainPreset::slots member. This TU uses no Qt signals/slots
// keywords (only registerTool lambdas), so the macro is dropped after the
// includes.
#include "../engine/ChainLibrary.h"
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
#include <vector>

// See note at the top of this file: drop Qt's `slots` macro so
// HDAW::ChainPreset::slots stays a plain member below.
#ifdef slots
#undef slots
#endif

namespace mcp {

void registerFxChainTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"save_fx_chain",
        "Save a track's entire FX chain as a named preset (slot types, order, params, bypass states, plugin states, sampler + psy-fm state).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"name",    QJsonObject{{"type","string"}}}}, {"trackId","name"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            QString name = a.value("name").toString();
            if (name.trimmed().isEmpty())
                return McpToolResult::text("name required", true);
            HDAW::ChainPreset p = e->getProjectCommands().exportFxChain(ti);
            p.name = juce::String(name.toStdString());
            juce::String id = HDAW::ChainLibrary::userLibrary().savePreset(p);
            if (id.isEmpty())
                return McpToolResult::text("failed to save chain preset", true);
            QJsonObject out;
            out["id"] = QString::fromStdString(id.toStdString());
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"list_fx_chains",
        "List saved FX chain presets (id, name, slotCount).",
        objSchema(QJsonObject{}, QJsonArray{}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            (void) e; (void) a;
            QJsonArray arr;
            for (const auto& p : HDAW::ChainLibrary::userLibrary().listPresets()) {
                QJsonObject o;
                o["id"] = QString::fromStdString(p.id.toStdString());
                o["name"] = QString::fromStdString(p.name.toStdString());
                o["slotCount"] = static_cast<int>(p.slots.size());
                arr.append(o);
            }
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"load_fx_chain",
        "Load a saved FX chain preset onto a track, replacing its chain in one undo unit. Give id or name (name must resolve to exactly one preset).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"id",      QJsonObject{{"type","string"}}},
                  {"name",    QJsonObject{{"type","string"}}}}, {"trackId"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            QString id = a.value("id").toString();
            QString name = a.value("name").toString();
            if (id.isEmpty() && name.isEmpty())
                return McpToolResult::text("load_fx_chain: id or name required", true);
            const auto& lib = HDAW::ChainLibrary::userLibrary();
            HDAW::ChainPreset preset;
            if (!id.isEmpty()) {
                // Both given: id wins (deterministic); name-only resolves below.
                preset = lib.loadPreset(juce::String(id.toStdString()));
                if (preset.id.isEmpty())
                    return McpToolResult::text("preset not found: " + id, true);
            } else {
                std::vector<HDAW::ChainPreset> matches;
                for (const auto& p : lib.listPresets()) {
                    if (QString::fromStdString(p.name.toStdString()) == name)
                        matches.push_back(p);
                }
                if (matches.empty())
                    return McpToolResult::text("preset not found: " + name, true);
                if (matches.size() > 1)
                    return McpToolResult::text("ambiguous preset name: " + name, true);
                preset = matches.front();
            }
            juce::String error;
            if (!e->getProjectCommands().applyFxChain(ti, preset, &error))
                return McpToolResult::text(QString::fromStdString(error.toStdString()), true);
            // NOTE: warnings is always empty. applyFxChain has no warnings
            // channel by design (Task 2 contract): a missing sampler sample
            // is an HDAW_LOG plus a slot applied without its sample — never
            // a silent pass, never a hard failure. The array exists for
            // schema stability so a future warnings channel needs no shape
            // change.
            QJsonObject out;
            out["ok"] = true;
            out["warnings"] = QJsonArray{};
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"delete_fx_chain",
        "Delete a saved FX chain preset.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            (void) e;
            QString id = a.value("id").toString();
            if (id.isEmpty())
                return McpToolResult::text("id required", true);
            if (!HDAW::ChainLibrary::userLibrary().deletePreset(juce::String(id.toStdString())))
                return McpToolResult::text("preset not found or not deletable: " + id, true);
            return McpToolResult::text("ok");
        }});

}

} // namespace mcp
