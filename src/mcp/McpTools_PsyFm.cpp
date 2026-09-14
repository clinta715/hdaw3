#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/PsyFmEngine.h"
#include "../engine/PsyFmModMatrix.h"
#include "../engine/PsyFmState.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

void registerPsyFmTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"psy_fm_load_preset",
        "Load a psytrance FM preset routing (growlBass, acidLead, metallicPluck, riser) into a psy_fm FX slot. "
        "Sets algorithm, modulation matrix, and default ratios/feedback/envelopes.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}},
            {"preset",   QJsonObject{{"type","string"},
                {"enum", QJsonArray{"growlBass","acidLead","metallicPluck","riser"}}}}
        }, {"trackId","slotIndex","preset"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QString preset = a.value("preset").toString();
            bool ok = e->getAudioEngineCommands().setFxSlotPsyFmPreset(ti, si, preset.toStdString());
            return ok ? McpToolResult::text("loaded preset: " + preset)
                      : McpToolResult::text("unknown preset: " + preset, true);
        }});

s.registerTool({"psy_fm_get_analysis",
        "Get the current analysis data from a psy_fm FX slot (active voices, per-operator EG levels). "
        "Returns live:false when the audio engine is unavailable (no audio device).",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}}
        }, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QJsonObject state;
            state["live"] = false;
            state["activeVoices"] = 0;
            QJsonArray opLevels;
            for (int op = 0; op < 6; op++)
                opLevels.append(0.0);
            state["opEgLevels"] = opLevels;

            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < (int)chain.size() && chain[si])
                    {
                        auto* engine = chain[si]->psyFmEngine();
                        if (engine)
                        {
                            state["live"] = true;
                            state["activeVoices"] = engine->activeVoiceCount();
                            opLevels = QJsonArray();
                            for (int op = 0; op < 6; op++)
                                opLevels.append(static_cast<double>(engine->getOpEgLevel(op)));
                            state["opEgLevels"] = opLevels;
                        }
                    }
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"psy_fm_set_mod_route",
        "Add or update a modulation route on a psy_fm FX slot's modulation matrix. "
        "Routes are persisted in the project tree and survive save/load/rebuild.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}},
            {"source",    QJsonObject{{"type","string"},
                {"enum", QJsonArray{"ratioSweepLFO","feedbackLFO","modWheel","velocity","barClock"}}}},
            {"dest",      QJsonObject{{"type","string"},
                {"enum", QJsonArray{"op1Ratio","op2Ratio","op3Ratio","op4Ratio","op5Ratio","op6Ratio","op6Feedback"}}}},
            {"depth",     QJsonObject{{"type","number"}}}
        }, {"trackId","slotIndex","source","dest","depth"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QString srcStr = a.value("source").toString();
            QString destStr = a.value("dest").toString();
            float depth = static_cast<float>(a.value("depth").toDouble());

            e->getAudioEngineCommands().setFxSlotPsyFmModRoute(ti, si,
                srcStr.toStdString(), destStr.toStdString(), depth);
            return McpToolResult::text("ok");
        }});

s.registerTool({"psy_fm_mod_matrix_debug",
        "Read-only debug view of a psy_fm FX slot's modulation matrix. For each route, "
        "reports the current source value, the raw contribution (sourceValue * depth), and the "
        "budget-scaled contribution actually applied. Op6Feedback routes share a per-destination "
        "budget: when the summed |depth| exceeds 1.0, every feedback contribution is scaled by "
        "1/total. Also returns base vs computed (simulated apply()) ratios/feedback and the live "
        "source values. No mutation, no audio render.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}}
        }, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            using SR = HDAW::PsyFmModRoute::Source;
            using DR = HDAW::PsyFmModRoute::Dest;

            // Source-name mapping: the matrix engine indexes the pool directly
            // (0 ratioSweepLFO, 1 feedbackLFO, 2 modWheel, 3 velocity); BarClock
            // exists only as a track-level mod target and is not a pool source.
            auto sourceNameUpper = [](SR s) -> const char* {
                switch (s) {
                    case SR::RatioSweepLFO: return "RatioSweepLFO";
                    case SR::FeedbackLFO:   return "FeedbackLFO";
                    case SR::ModWheel:      return "ModWheel";
                    case SR::Velocity:      return "Velocity";
                    case SR::BarClock:      return "BarClock";
                }
                return "RatioSweepLFO";
            };
            auto destNameUpper = [](DR d) -> const char* {
                switch (d) {
                    case DR::Op1Ratio:  return "Op1Ratio";
                    case DR::Op2Ratio:  return "Op2Ratio";
                    case DR::Op3Ratio:  return "Op3Ratio";
                    case DR::Op4Ratio:  return "Op4Ratio";
                    case DR::Op5Ratio:  return "Op5Ratio";
                    case DR::Op6Ratio:  return "Op6Ratio";
                    case DR::Op6Feedback: return "Op6Feedback";
                    case DR::RatioSweepRateItself: return "RatioSweepRateItself";
                }
                return "Op1Ratio";
            };
            // Mirrors PsyFmModMatrix::sourceIndexFor (BarClock is not a pool source).
            auto poolIndexFor = [](SR s) -> int {
                switch (s) {
                    case SR::RatioSweepLFO: return 0;
                    case SR::FeedbackLFO:   return 1;
                    case SR::ModWheel:      return 2;
                    case SR::Velocity:      return 3;
                    default: return -1;
                }
            };

            // Snapshot: base params from the slot tree (read-only), routes +
            // source pool from the live engine under matrixLock_ when available.
            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (!slotTree.isValid())
                return McpToolResult::text("slot tree not found", true);

            float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
            float baseFeedback = 0.0f;
            for (int i = 0; i < 6; ++i)
                baseRatios[i] = static_cast<float>(static_cast<double>(
                    slotTree.getProperty(juce::Identifier("param_" + juce::String(i)),
                                         baseRatios[i])));
            baseFeedback = static_cast<float>(static_cast<double>(
                slotTree.getProperty(juce::Identifier("param_6"), baseFeedback)));

            HDAW::PsyFmModSourcePool pool;      // defaults (phase 0, wheel/vel 0)
            std::vector<HDAW::PsyFmModRoute> routes;
            bool live = false;
            if (auto* proc = e->getMainProcessor())
            {
                if (auto* track = proc->getTrack(ti))
                {
                    auto& chain = track->getFXChain();
                    if (si < (int)chain.size() && chain[si])
                    {
                        if (auto* psyFm = chain[si]->psyFmEngine())
                        {
                            psyFm->snapshotModState(routes, baseRatios, baseFeedback, pool);
                            live = true;
                        }
                    }
                }
            }
            if (!live)
            {
                // No audio device / processor: fall back to the persisted tree
                // routes so the patch designer still gets a usable view.
                juce::String matrixStr = slotTree.getProperty("psyFmMatrix", "").toString();
                routes = HDAW::PsyFmState::decodeRoutes(matrixStr.toStdString());
                pool.ratioSweepLFORateHz = static_cast<float>(static_cast<double>(
                    slotTree.getProperty("psyFmSweepRate", (double)pool.ratioSweepLFORateHz)));
            }

            // Budget: per-destination depth budget on Op6Feedback (Bug 5 fix in
            // PsyFmModMatrix::apply — ratio destinations are additive, unbudgeted).
            float feedbackTotalDepth = 0.0f;
            for (const auto& r : routes)
                if (r.dest == DR::Op6Feedback)
                    feedbackTotalDepth += std::abs(r.depth);
            const float feedbackScale = (feedbackTotalDepth > 1.0f)
                ? 1.0f / feedbackTotalDepth : 1.0f;

            QJsonArray routeArr;
            for (const auto& r : routes)
            {
                QJsonObject ro;
                ro["source"] = sourceNameUpper(r.source);
                ro["dest"]   = destNameUpper(r.dest);
                ro["depth"]  = (double) r.depth;
                const float srcVal = (poolIndexFor(r.source) >= 0)
                    ? pool.getSourceValue(poolIndexFor(r.source)) : 0.0f;
                const float raw = srcVal * r.depth;
                ro["sourceValue"] = (double) srcVal;
                ro["rawContribution"] = (double) raw;
                const bool scaled = (r.dest == DR::Op6Feedback && feedbackTotalDepth > 1.0f);
                ro["budgetScaled"] = scaled;
                ro["scaledContribution"] = (double)(scaled ? raw * feedbackScale : raw);
                routeArr.append(ro);
            }

            // Simulate apply() on the snapshot copies — identical math to the
            // engine's block-rate matrix pass, without rendering audio.
            float outRatios[6];
            float outFeedback = 0.0f;
            HDAW::PsyFmModMatrix sim;
            for (const auto& r : routes)
                sim.addRoute(r);
            sim.apply(pool, baseRatios, baseFeedback, outRatios, outFeedback);

            QJsonArray baseArr, compArr;
            for (int i = 0; i < 6; ++i)
            {
                baseArr.append((double) baseRatios[i]);
                compArr.append((double) outRatios[i]);
            }

            QJsonObject out;
            out["live"] = live;
            out["routes"] = routeArr;

            QJsonObject budget;
            budget["totalDepth"] = (double) feedbackTotalDepth;
            budget["scaling"]    = (double) feedbackScale;
            budget["budgetHit"]  = feedbackTotalDepth > 1.0f;
            out["feedbackBudget"] = budget;

            QJsonObject baseParams;
            baseParams["ratios"] = baseArr;
            baseParams["feedback"] = (double) baseFeedback;
            out["baseParams"] = baseParams;

            QJsonObject compParams;
            compParams["ratios"] = compArr;
            compParams["feedback"] = (double) outFeedback;
            out["computedParams"] = compParams;

            QJsonObject srcVals;
            srcVals["ratioSweepLFO"] = (double) pool.getSourceValue(0);
            srcVals["feedbackLFO"]   = (double) pool.getSourceValue(1);
            srcVals["modWheel"]      = (double) pool.modWheelValue;
            srcVals["velocity"]      = (double) pool.velocityValue;
            out["sourceValues"] = srcVals;

            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"psy_fm_clear_mod_matrix",
        "Clear all modulation routes on a psy_fm FX slot.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}}
        }, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            e->getAudioEngineCommands().clearFxSlotPsyFmModRoutes(ti, si);
            return McpToolResult::text("ok");
        }});

} // namespace registerPsyFmTools

} // namespace mcp
