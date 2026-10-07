// ChainLibrary.h MUST stay the first include: Qt defines a `slots` macro
// (qobjectdefs.h, via McpServer.h/QObject below) that would otherwise rewrite
// the HDAW::ChainPreset::slots member. This TU uses no Qt signals/slots
// keywords (only registerTool lambdas), so the macro is dropped after the
// includes.
#include "../engine/ChainLibrary.h"
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
// SLOT-SCOPED PATCH verbs: the ONE shared reader/body/command call the
// project.savePatch / project.loadPatch / project.listPatches RPC routes share
// (src/common/PatchPreset.h), so the payload and every refusal are
// byte-identical on both surfaces by construction.
#include "../common/PatchPreset.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
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
        "Save a track's entire FX chain as a named preset (slot types, order, params, bypass states, plugin states, sampler + psy-fm state). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"trackID", QJsonObject{{"type","integer"}}},
                  {"name",    QJsonObject{{"type","string"}}}}, {"name"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
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
        "List FX chain presets (id, name, slotCount, source). Includes built-in factory presets (source \"factory\": psytrance per-role chains shipped with HDAW, e.g. Kick Punch, Bass Glue, Acid Lead; they can be listed and loaded but never deleted) and user-saved presets (source \"user\").",
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
                o["source"] = p.isFactory ? QString("factory") : QString("user");
                arr.append(o);
            }
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"load_fx_chain",
        "Load a saved FX chain preset onto a track: instrument slots (sampler/sub_synth/psy_fm/fm_synth/growl_bass/psyarp/drum_synth/reese_bass) are preserved; FX slots are replaced and appended after them in one undo unit. Give id or name (name must resolve to exactly one preset). Ids may be user presets or built-in factory presets (\"_factory/<File_Name>.json\" from list_fx_chains — the 11 psytrance per-role chains); name resolution covers factory presets too. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"trackID", QJsonObject{{"type","integer"}}},
                  {"id",      QJsonObject{{"type","string"}}},
                  {"name",    QJsonObject{{"type","string"}}}}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
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
        "Delete a saved FX chain preset. Built-in factory presets (id starting with \"_factory/\") cannot be deleted — the request fails and the file stays.",
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

// ── Slot-scoped PATCH verbs (the SLOT-SCOPED sibling of the chain tools) ────
// A patch is ONE slot's full state; applyPatch writes it INTO the addressed
// slot — never appends, never removes. Both surfaces run the SAME shared body
// (common/PatchPreset.h), so the payload and every refusal are byte-identical
// by construction (AGENTS.md "RPC parity by construction").
s.registerTool({"save_patch",
        "Save ONE FX slot's full state as a named patch (params, plugin state, sampler + slice, psy-fm matrix/sweep). " +
        mcp::stableRefRuleText("trackID", "trackId") +
        " Returns compact JSON {\"id\":\"user/<name>.json\"}.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"name",      QJsonObject{{"type","string"}}}},
                  {"slotIndex","name"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto r = HDAW::savePatchToolText(*e, a);
            return McpToolResult::text(r.text, !r.ok);
        }});

s.registerTool({"load_patch",
        "Load a saved patch INTO the addressed FX slot: params, plugin state, sampler + slice and psy-fm state are rewritten. The slot must already exist and match the patch's fxType — the patch never appends or removes a slot. Ids may be user patches or built-in factory patches (\"_factory/<Name>.json\" from list_patches — the 12 shipped bass patches). " +
        mcp::stableRefRuleText("trackID", "trackId") +
        " An unknown patch id is refused.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"id",        QJsonObject{{"type","string"}}}},
                  {"slotIndex","id"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto r = HDAW::loadPatchToolText(*e, a);
            return McpToolResult::text(r.text, !r.ok);
        }});

s.registerTool({"list_patches",
        "List saved slot patches (id, name, fxType, slotCount, source). Includes built-in factory patches (source \"factory\": 12 bass patches shipped with HDAW — 6 reese_bass, 4 growl_bass, 2 sub_synth, e.g. Reese Classic, Growl Rolling Sub, Sub Pure; they can be listed and loaded but never deleted) and user-saved patches (source \"user\").",
        objSchema(QJsonObject{}, QJsonArray{}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto r = HDAW::listPatchesToolText(*e, a);
            return McpToolResult::text(r.text, !r.ok);
        }});

}

} // namespace mcp
