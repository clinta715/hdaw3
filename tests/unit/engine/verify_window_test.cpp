// S4 of docs/plans/2026-09-28-agent-mechanization.md (§5): verify_window and
// render_and_verify — the render→measure→compare tools, and their MCP <-> RPC
// twins.
//
// The DISCRIMINATING point of verify_window is what it does NOT measure:
// it renders the WHOLE project through the export path (a window-ONLY render is
// not predictive — plugin state re-bakes per window, and the v0.39.2 close-out
// measured 0 clamps on a windowed render of a file that carried 32 exact-FS
// frames: docs/handoffs/2026-09-28-v0.39.2-backlog-closeout.md §3), then
// measures ONLY the requested window and gates THE WINDOW's own metrics. So:
//   * clamps OUTSIDE the window  -> verify_window PASSES, mix_report FAILS;
//   * a clamp INSIDE the window  -> verify_window FAILS.
// Both fixtures force the clamp with a deterministic clip GAIN (a 0.2-amplitude
// sine at clip gain 8.0 -> 1.6 -> the 24-bit export clamps at full scale, so
// per-channel |x| >= 0.999 and ceilingHitFrames counts).
//
// Audibility comes FIRST (lesson 25): a silent window would make every other
// comparison vacuous, so the quiet-window measurement is asserted above the
// -80 dBFS floor before any gate verdict is trusted.

#include <gtest/gtest.h>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/ReadModel.h"
#include "common/ProjectCommands.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <string>

namespace {

// BPM 120 -> 1 beat = 0.5 s. Layout of the discriminating fixture:
//   track "Loud"  : clip at beats [0,4)   (2 s) gain 8.0  -> exact-FS clamps
//   track "Quiet" : clip at beats [8,12)  (4 s..6 s) gain 1.0 -> no clamps
// Project duration = maxEnd + 3 s = 9 s, so both windows fall inside the file.
constexpr double kBeatsPerSecond = 2.0;   // at 120 BPM

class RenderFixtureBase : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        for (const auto& f : tempFiles)
            juce::File(f.toStdString()).deleteFile();
    }

    // A deterministic WAV: `seconds` of a sine at `amplitude`, in the temp dir.
    QString makeWav(const QString& tag, float amplitude, double seconds = 3.0) {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const QString name = QStringLiteral("hdaw_verify_window_fixture_%1_%2.wav")
                                 .arg(tag)
                                 .arg(juce::Random::getSystemRandom().nextInt());
        const auto file = dir.getChildFile(juce::String(name.toUtf8().constData()));
        const double sr = 48000.0;
        const int len = static_cast<int>(sr * seconds);
        juce::AudioBuffer<float> buf(2, len);
        for (int s = 0; s < len; ++s) {
            const float v = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi
                                                 * 220.0f * static_cast<float>(s) / static_cast<float>(sr));
            buf.setSample(0, s, v);
            buf.setSample(1, s, v);
        }
        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wavFmt.createWriterFor(new juce::FileOutputStream(file), sr, 2, 24, {}, 0));
        EXPECT_NE(w, nullptr);
        if (w != nullptr)
            w->writeFromAudioSampleBuffer(buf, 0, len);
        tempFiles.push_back(QString::fromUtf8(file.getFullPathName().toRawUTF8()));
        return tempFiles.back();
    }

    QString tempWavPath(const QString& tag) {
        const QString name = QStringLiteral("hdaw_verify_window_out_%1_%2.wav")
                                 .arg(tag)
                                 .arg(juce::Random::getSystemRandom().nextInt());
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile(juce::String(name.toUtf8().constData()));
        file.deleteFile();
        tempFiles.push_back(QString::fromUtf8(file.getFullPathName().toRawUTF8()));
        return tempFiles.back();
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call", QJsonObject{{"name", tool}, {"arguments", args}})
            .toObject();
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto content = mcpResult(tool, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool mcpIsError(const QString& tool, const QJsonObject& args) {
        return mcpResult(tool, args).value("isError").toBool();
    }
    QJsonValue mcpValue(const QString& tool, const QJsonObject& args) {
        const QString text = mcpText(tool, args);
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray()) return QJsonValue(doc.array());
        if (doc.isObject()) return QJsonValue(doc.object());
        return QJsonValue(text);
    }

    // --- RPC surface -------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonValue rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError) << "RPC " << method.toStdString() << " errored: "
                                << r.payload.toObject().value("message").toString().toStdString();
        return r.payload;
    }
    void expectSameFailure(const QString& tool, const QString& method,
                           const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        const QString rpcMessage = r.payload.toObject().value("message").toString();
        const QString mcpMessage = mcpText(tool, args);
        EXPECT_FALSE(mcpMessage.isEmpty()) << "a failure must carry a reason";
        EXPECT_EQ(rpcMessage, mcpMessage);
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::vector<QString> tempFiles;
};

// ── verify_window: the discriminating fixtures ─────────────────────────────
class VerifyWindowParity : public RenderFixtureBase {
protected:
    void SetUp() override {
        RenderFixtureBase::SetUp();
        auto& c = engine->getProjectCommands();
        c.setTempo(120.0);

        const int loudTrack = c.addTrack("Loud");
        ASSERT_GE(loudTrack, 0);
        const int quietTrack = c.addTrack("Quiet");
        ASSERT_GE(quietTrack, 0);

        const QString loudWav = makeWav("loud", 0.2f);
        const QString quietWav = makeWav("quiet", 0.05f);

        // addAudioClip speaks BEATS (converted at the project BPM).
        loudClip = c.addAudioClip(loudTrack, 0.0, 4.0, loudWav.toStdString(), "Loud");
        ASSERT_GT(loudClip, 0);
        quietClip = c.addAudioClip(quietTrack, 8.0, 4.0, quietWav.toStdString(), "Quiet");
        ASSERT_GT(quietClip, 0);

        // The clamp mechanism: 0.2 amplitude x gain 8.0 = 1.6 -> full-scale clamp.
        c.setClipGain(loudClip, 8.0f);
        c.setClipGain(quietClip, 1.0f);
        engine->drainPendingRoutingRebuild();
    }

    int loudClip = -1, quietClip = -1;
};

// (a) DISCRIMINATING FIXTURE 1: clamps OUTSIDE the window, none inside.
// verify_window {ceilingHitPctMax: 0} PASSES for the quiet window while
// mix_report over the SAME full render FAILS — which is the whole point: the
// file really does clamp, but not in the window.
TEST_F(VerifyWindowParity, VerifyWindowGatesTheWindowWhileMixReportGatesTheFile) {
    const QString wav = tempWavPath("outside");
    const QJsonObject targets{{"ceilingHitPctMax", 0.0}};
    const QJsonObject args{{"startBeat", 8.0}, {"endBeat", 12.0},
                           {"targets", targets}, {"outputPath", wav}};

    const QJsonValue viaMcp = mcpValue("verify_window", args);
    const QJsonValue viaRpc = rpcPayload("composition.verifyWindow", args);
    EXPECT_EQ(viaRpc, viaMcp) << "one shared command => byte-identical payload";

    const QJsonObject p = viaRpc.toObject();
    ASSERT_TRUE(p.value("ok").toBool())
        << ("verify_window must succeed on a valid window (error: "
            + p.value("error").toString().toStdString() + ")");
    EXPECT_EQ(p.value("wavPath").toString(), wav);
    EXPECT_TRUE(QFileInfo::exists(wav)) << "the WAV is kept for A/B (the caller deletes it)";

    const QJsonObject report = p.value("report").toObject();

    // (d) AUDIBILITY FIRST (lesson 25): a silent window voids every comparison.
    const double windowRms = report.value("rms").toDouble();
    EXPECT_GT(windowRms, 1e-4) << "the quiet window must actually carry audio (rms > -80 dBFS)";
    // (d) the reported window span matches the requested beats within tolerance.
    const QJsonObject window = p.value("window").toObject();
    EXPECT_NEAR(window.value("durationSec").toDouble(), 4.0 / kBeatsPerSecond, 0.05);
    EXPECT_DOUBLE_EQ(window.value("startSec").toDouble(), 4.0);
    EXPECT_DOUBLE_EQ(window.value("endSec").toDouble(), 6.0);

    // The WINDOW is clamp-free -> the target passes.
    EXPECT_TRUE(p.value("targetsOk").toBool()) << "no clamp inside the window";
    EXPECT_DOUBLE_EQ(report.value("ceilingHitPct").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(report.value("ceilingHitFrames").toDouble(), 0.0);

    // The SAME render, measured WHOLE-FILE, fails the same target: the clamp
    // lives outside the window (that is the discrimination).
    const QJsonValue whole = mcpValue("mix_report", QJsonObject{
        {"filePath", wav}, {"targets", targets}});
    ASSERT_TRUE(whole.isObject());
    EXPECT_GT(whole.toObject().value("ceilingHitPct").toDouble(), 0.0)
        << "the full render must carry the clamp the window does not";
    EXPECT_FALSE(whole.toObject().value("targetsOk").toBool())
        << "mix_report over the whole file must FAIL the same ceiling target";
}

// (b) DISCRIMINATING FIXTURE 2: a clamp INSIDE the window -> verify_window FAILS.
TEST_F(VerifyWindowParity, VerifyWindowFailsWhenTheClampIsInsideTheWindow) {
    const QString wav = tempWavPath("inside");
    const QJsonObject targets{{"ceilingHitPctMax", 0.0}};
    const QJsonObject args{{"startBeat", 0.0}, {"endBeat", 4.0},
                           {"targets", targets}, {"outputPath", wav}};

    const QJsonValue viaMcp = mcpValue("verify_window", args);
    const QJsonValue viaRpc = rpcPayload("composition.verifyWindow", args);
    EXPECT_EQ(viaRpc, viaMcp);

    const QJsonObject p = viaRpc.toObject();
    ASSERT_TRUE(p.value("ok").toBool());
    const QJsonObject report = p.value("report").toObject();
    EXPECT_GT(report.value("rms").toDouble(), 1e-4) << "audible before judging the verdict";
    EXPECT_GT(report.value("ceilingHitFrames").toDouble(), 0.0)
        << "the clamped clip sounds inside this window";
    EXPECT_GT(report.value("ceilingHitPct").toDouble(), 0.0);
    EXPECT_FALSE(p.value("targetsOk").toBool())
        << "a clamp inside the window must FAIL the ceiling target";
    EXPECT_FALSE(p.value("targetChecks").toArray().isEmpty())
        << "the failing check must be reported";
}

// (c) The inverted/empty window is refused with the SAME bytes on both
// surfaces, and the refusal happens BEFORE any render.
TEST_F(VerifyWindowParity, InvertedWindowFailsIdenticallyOnBothSurfaces) {
    expectSameFailure("verify_window", "composition.verifyWindow",
                      QJsonObject{{"startBeat", 12.0}, {"endBeat", 8.0}});
    EXPECT_EQ(mcpText("verify_window", QJsonObject{{"startBeat", 12.0}, {"endBeat", 8.0}}),
              QString("startBeat/endBeat invalid: need startBeat < endBeat"));

    // Empty window (end == start) is the same refusal.
    expectSameFailure("verify_window", "composition.verifyWindow",
                      QJsonObject{{"startBeat", 4.0}, {"endBeat", 4.0}});

    // A MISSING required beat fails with the MCP validator's exact bytes on both
    // surfaces (the shared parser reproduces them).
    expectSameFailure("verify_window", "composition.verifyWindow", QJsonObject{{"endBeat", 4.0}});
    EXPECT_EQ(mcpText("verify_window", QJsonObject{{"endBeat", 4.0}}),
              QString("invalid params: startBeat: missing required property 'startBeat'"));
}

// The `expect` key is the accepted alias of `targets` on both surfaces, so the
// plan's literal `verify_window {.., expect:{ceilingHitPctMax:0}}` form works.
TEST_F(VerifyWindowParity, ExpectIsAnAcceptedAliasOfTargets) {
    const QString wav = tempWavPath("alias");
    const QJsonObject args{{"startBeat", 8.0}, {"endBeat", 12.0},
                           {"expect", QJsonObject{{"ceilingHitPctMax", 0.0}}},
                           {"outputPath", wav}};
    const QJsonValue viaMcp = mcpValue("verify_window", args);
    const QJsonValue viaRpc = rpcPayload("composition.verifyWindow", args);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_TRUE(viaRpc.toObject().value("targetsOk").toBool());
    ASSERT_FALSE(viaRpc.toObject().value("targetChecks").toArray().isEmpty());
}

// ITEM 3(a): `rmsMin` is a FLOOR on the window's linear RMS (the same units as
// the report's root `rms`). A floor above the actual rms FAILS with the row
// present; below it PASSES. The fixture's rms is non-zero so this is meaningful.
TEST_F(VerifyWindowParity, RmsMinFloorsTheWindowRms) {
    const QString wav = tempWavPath("rmsmin-measure");
    const QJsonValue measured = mcpValue("verify_window", QJsonObject{
        {"startBeat", 8.0}, {"endBeat", 12.0}, {"outputPath", wav}});
    ASSERT_TRUE(measured.isObject());
    const double rms = measured.toObject().value("report").toObject().value("rms").toDouble();
    EXPECT_GT(rms, 1e-4) << "a silent render would make the floor vacuous";

    const QString failWav = tempWavPath("rmsmin-fail");
    const QJsonObject failArgs{{"startBeat", 8.0}, {"endBeat", 12.0},
                               {"targets", QJsonObject{{"rmsMin", rms * 2.0}}},
                               {"outputPath", failWav}};
    const QJsonValue viaMcp = mcpValue("verify_window", failArgs);
    const QJsonValue viaRpc = rpcPayload("composition.verifyWindow", failArgs);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_FALSE(viaRpc.toObject().value("targetsOk").toBool())
        << "a floor above the actual rms must FAIL";
    bool rowFound = false;
    for (const auto& r : viaRpc.toObject().value("targetChecks").toArray()) {
        const auto row = r.toObject();
        if (row.value("target").toString() != "rmsMin") continue;
        rowFound = true;
        EXPECT_FALSE(row.value("pass").toBool());
        EXPECT_EQ(row.value("op").toString(), QString("at least"));
        EXPECT_DOUBLE_EQ(row.value("expected").toDouble(), rms * 2.0);
        EXPECT_NEAR(row.value("actual").toDouble(), rms, 1e-6);
    }
    EXPECT_TRUE(rowFound) << "the rmsMin check row must be reported";

    const QString passWav = tempWavPath("rmsmin-pass");
    const QJsonValue pass = rpcPayload("composition.verifyWindow", QJsonObject{
        {"startBeat", 8.0}, {"endBeat", 12.0},
        {"targets", QJsonObject{{"rmsMin", rms * 0.5}}}, {"outputPath", passWav}});
    EXPECT_TRUE(pass.toObject().value("targetsOk").toBool())
        << "a floor below the actual rms must PASS";
}

// ITEM 3(b): `rmsMin` is a LINEAR RMS floor (same units as the report's root
// `rms`). A silent window reports rms 0 and therefore FAILS any POSITIVE floor;
// a floor of 0.0 passes (the check is `>=`), and a floor below an audible
// window's rms passes.
TEST_F(VerifyWindowParity, RmsMinFailsSilenceAndPassesAudible) {
    // Beats 4..8 are the silent gap between the two clips.
    const QString silentWav = tempWavPath("rmsmin-silent");
    const QJsonObject silent{{"startBeat", 4.0}, {"endBeat", 8.0},
                             {"targets", QJsonObject{{"rmsMin", 0.01}}},
                             {"outputPath", silentWav}};
    const QJsonValue s = rpcPayload("composition.verifyWindow", silent);
    ASSERT_TRUE(s.toObject().value("ok").toBool());
    EXPECT_DOUBLE_EQ(s.toObject().value("report").toObject().value("rms").toDouble(), 0.0)
        << "the gap window must be silent";
    EXPECT_FALSE(s.toObject().value("targetsOk").toBool())
        << "a positive linear floor must FAIL a silent window";
    bool rowFound = false;
    for (const auto& r : s.toObject().value("targetChecks").toArray()) {
        const auto row = r.toObject();
        if (row.value("target").toString() != "rmsMin") continue;
        rowFound = true;
        EXPECT_FALSE(row.value("pass").toBool());
        EXPECT_EQ(row.value("op").toString(), QString("at least"));
        EXPECT_DOUBLE_EQ(row.value("expected").toDouble(), 0.01);
        EXPECT_DOUBLE_EQ(row.value("actual").toDouble(), 0.0);
    }
    EXPECT_TRUE(rowFound) << "the rmsMin check row must be reported";

    // A floor of exactly 0.0 PASSES the silent window (rms >= 0).
    const QJsonValue zero = rpcPayload("composition.verifyWindow", QJsonObject{
        {"startBeat", 4.0}, {"endBeat", 8.0},
        {"targets", QJsonObject{{"rmsMin", 0.0}}}, {"outputPath", tempWavPath("rmsmin-zero")}});
    EXPECT_TRUE(zero.toObject().value("targetsOk").toBool())
        << "a 0.0 linear floor must PASS a silent window";

    // A floor below an AUDIBLE window's rms also passes.
    const QJsonValue measured = mcpValue("verify_window", QJsonObject{
        {"startBeat", 8.0}, {"endBeat", 12.0}, {"outputPath", tempWavPath("rmsmin-measure2")}});
    const double audibleRms = measured.toObject().value("report").toObject().value("rms").toDouble();
    EXPECT_GT(audibleRms, 1e-4) << "the audible window must carry audio";
    const QJsonValue a = rpcPayload("composition.verifyWindow", QJsonObject{
        {"startBeat", 8.0}, {"endBeat", 12.0},
        {"targets", QJsonObject{{"rmsMin", audibleRms * 0.5}}},
        {"outputPath", tempWavPath("rmsmin-audible")}});
    EXPECT_TRUE(a.toObject().value("targetsOk").toBool())
        << "a floor below the audible window's rms must PASS";
}

// ITEM 3(c): an UNKNOWN expectation key is refused with ONE shared text on both
// surfaces, and the refusal happens BEFORE any render (no WAV written).
TEST_F(VerifyWindowParity, UnknownExpectationKeyIsRefusedBeforeAnyRender) {
    const QString wav = tempWavPath("strict-key");
    const QJsonObject args{{"startBeat", 8.0}, {"endBeat", 12.0},
                           {"targets", QJsonObject{{"totallyBogusKey", 1}}},
                           {"outputPath", wav}};
    expectSameFailure("verify_window", "composition.verifyWindow", args);
    EXPECT_EQ(mcpText("verify_window", args),
              QString("unknown expectation key totallyBogusKey"));
    EXPECT_FALSE(QFileInfo::exists(wav))
        << "the strict-key refusal must happen BEFORE the render (no WAV written)";

    // ITEM A: the removed `rmsMinDb` key is now an UNKNOWN key on both surfaces,
    // and is refused before any render — no silent no-op green verdict.
    const QString dbWav = tempWavPath("strict-rmsmindb");
    const QJsonObject dbArgs{{"startBeat", 8.0}, {"endBeat", 12.0},
                             {"targets", QJsonObject{{"rmsMinDb", -20.0}}},
                             {"outputPath", dbWav}};
    expectSameFailure("verify_window", "composition.verifyWindow", dbArgs);
    EXPECT_EQ(mcpText("verify_window", dbArgs),
              QString("unknown expectation key rmsMinDb"));
    EXPECT_FALSE(QFileInfo::exists(dbWav))
        << "the rmsMinDb refusal must happen BEFORE the render (no WAV written)";

    // The plan's own `rmsMin` is now RECOGNIZED (it used to be silently ignored,
    // returning a green ok:true for a gate that never ran).
    const QString wav2 = tempWavPath("strict-recognized");
    const QJsonValue recognized = mcpValue("verify_window", QJsonObject{
        {"startBeat", 8.0}, {"endBeat", 12.0},
        {"expect", QJsonObject{{"rmsMin", 0.0}}}, {"outputPath", wav2}});
    ASSERT_TRUE(recognized.isObject()) << mcpText("verify_window", QJsonObject{}).toStdString();
    EXPECT_TRUE(recognized.toObject().value("targetsOk").toBool());
}

// ── render_and_verify ──────────────────────────────────────────────────────
class RenderAndVerifyParity : public RenderFixtureBase {
protected:
    void SetUp() override {
        RenderFixtureBase::SetUp();
        auto& c = engine->getProjectCommands();
        c.setTempo(120.0);
        const int track = c.addTrack("Quiet");
        ASSERT_GE(track, 0);
        const QString wav = makeWav("rv", 0.05f);
        ASSERT_GT(c.addAudioClip(track, 0.0, 4.0, wav.toStdString(), "Quiet"), 0);
        // The verdict now includes the modulation gate (render_and_verify = render
        // + the SAME mix_verdict composition), so the sounding track must carry
        // movement or the release gate would (correctly) fail.
        c.addLfo(track);
        engine->drainPendingRoutingRebuild();
    }
};

// Twin: the same args on both surfaces produce the same payload, and the tool
// pair really renders a file whose audible gate passes.
TEST_F(RenderAndVerifyParity, TwinPayloadMatchesAndTheFileIsAudible) {
    const QString wav = tempWavPath("twin");
    const QJsonObject args{{"outputPath", wav}};

    const QJsonValue viaMcp = mcpValue("render_and_verify", args);
    const QJsonValue viaRpc = rpcPayload("export.renderAndVerify", args);
    EXPECT_EQ(viaRpc, viaMcp) << "one shared implementation => identical payload";

    const QJsonObject p = viaRpc.toObject();
    EXPECT_EQ(p.value("wavPath").toString(), wav);
    EXPECT_TRUE(QFileInfo::exists(wav));

    const QJsonObject gates = p.value("verdict").toObject().value("gates").toObject();
    ASSERT_TRUE(gates.contains("audible"));
    EXPECT_TRUE(gates.value("audible").toObject().value("ok").toBool())
        << "the produced file must be audible before the verdict means anything";
    EXPECT_TRUE(p.value("verdict").toObject().value("ok").toBool())
        << "a quiet, clamp-free render should pass the release gates";
}

// The refusal cases are byte-identical on both surfaces: the tool AND the route
// run the SAME argument parser (common/RenderToolArgs.h), which reproduces the
// MCP schema validator's exact bytes for a missing required property.
TEST_F(RenderAndVerifyParity, RefusalMatchesOnBothSurfaces) {
    // Missing outputPath — the validator's wording on both surfaces.
    expectSameFailure("render_and_verify", "export.renderAndVerify", QJsonObject{});
    EXPECT_EQ(mcpText("render_and_verify", QJsonObject{}),
              QString("invalid params: outputPath: missing required property 'outputPath'"));

    // Present but empty — the handler's own refusal, same bytes on both.
    const QJsonObject empty{{"outputPath", ""}};
    expectSameFailure("render_and_verify", "export.renderAndVerify", empty);
    EXPECT_EQ(mcpText("render_and_verify", empty), QString("outputPath required"));
}

// ITEM C: render_and_verify's DEFAULTS now match mix_verdict's (`fromPlan`
// false), so the NO-ARGS calls agree even on a project WITH a song plan; and
// with `fromPlan:true` passed to BOTH they still agree (plan gates included).
// Asserted on the serialized payload, byte for byte.
TEST_F(RenderAndVerifyParity, VerdictEqualsMixVerdictWithAPlan) {
    auto& c = engine->getProjectCommands();
    ProjectCommands::SongPlanData plan;
    plan.bpm = 120.0;
    plan.keyRoot = 0;
    plan.scaleMode = 1;
    plan.style = "psy";
    plan.seed = 1;
    plan.totalBars = 2;
    // 1-bar sections (2 s each at 120 BPM) so BOTH windows land inside the ~5 s
    // render, and a build->drop pair makes the loudness gate present.
    ProjectCommands::SongPlanSection s1; s1.name = "B"; s1.kind = "build"; s1.bars = 1;
    ProjectCommands::SongPlanSection s2; s2.name = "M"; s2.kind = "mainA"; s2.bars = 1;
    plan.sections.push_back(s1);
    plan.sections.push_back(s2);
    const auto set = c.setSongPlan(plan);
    ASSERT_TRUE(set.ok) << set.error;

    const auto compact = [](const QJsonValue& v) {
        return QString::fromUtf8(
            QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    };

    // (a) NO extra args on EITHER call. Both default to fromPlan:false, so they
    // must agree byte for byte DESPITE the song plan existing (before ITEM C they
    // disagreed: render_and_verify defaulted to fromPlan:true).
    const QString wav = tempWavPath("equal-plan-default");
    const QJsonValue rv = mcpValue("render_and_verify", QJsonObject{{"outputPath", wav}});
    ASSERT_TRUE(rv.isObject())
        << mcpText("render_and_verify", QJsonObject{{"outputPath", wav}}).toStdString();
    const QJsonObject verdict = rv.toObject().value("verdict").toObject();
    const QJsonValue mv = rpcPayload("audio.mixVerdict", QJsonObject{{"filePath", wav}});
    EXPECT_EQ(compact(mv), compact(verdict))
        << "no-arg render_and_verify must equal no-arg mix_verdict (both fromPlan:false)";
    // The modulation gate is the global rule the verdict ALWAYS owns.
    EXPECT_TRUE(verdict.value("gates").toObject().contains("modulation"));

    // (b) `fromPlan:true` passed to BOTH: the plan gates appear and still agree.
    const QJsonObject rvTrueArgs{{"outputPath", tempWavPath("equal-plan-true")},
                                 {"fromPlan", true}};
    const QJsonValue rv2 = mcpValue("render_and_verify", rvTrueArgs);
    ASSERT_TRUE(rv2.isObject()) << mcpText("render_and_verify", rvTrueArgs).toStdString();
    const QJsonObject verdict2 = rv2.toObject().value("verdict").toObject();
    const QJsonObject gates2 = verdict2.value("gates").toObject();
    EXPECT_TRUE(gates2.contains("modulation")) << "the modulation gate must be included";
    EXPECT_TRUE(gates2.contains("structure")) << "the structure gate must be included";
    EXPECT_TRUE(gates2.contains("loudness")) << "the loudness gate must be included";
    const QJsonValue mv2 = rpcPayload("audio.mixVerdict",
                                      QJsonObject{{"filePath", rvTrueArgs.value("outputPath")},
                                                  {"fromPlan", true}});
    EXPECT_EQ(compact(mv2), compact(verdict2))
        << "render_and_verify{fromPlan:true} must equal mix_verdict{fromPlan:true} byte for byte";
}

// Plan-less path: `fromPlan:false` (the DEFAULT on both surfaces now) agrees,
// and an EXPLICIT `fromPlan:true` on a plan-less project FALLS BACK to the
// whole-file verdict — it has already rendered, so it must still answer.
TEST_F(RenderAndVerifyParity, VerdictEqualsMixVerdictWithoutAPlan) {
    const QString wav = tempWavPath("equal-planless");
    const QJsonValue rv = mcpValue("render_and_verify",
                                   QJsonObject{{"outputPath", wav}, {"fromPlan", false}});
    ASSERT_TRUE(rv.isObject());
    const QJsonValue mv = rpcPayload("audio.mixVerdict",
                                     QJsonObject{{"filePath", wav}, {"fromPlan", false}});
    EXPECT_EQ(QString::fromUtf8(QJsonDocument(mv.toObject()).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument(rv.toObject().value("verdict").toObject())
                                    .toJson(QJsonDocument::Compact)));

    // The DEFAULT no-arg call (now fromPlan:false) also agrees with the plan-less
    // mix_verdict, and an explicit fromPlan:true with NO plan falls back rather
    // than refusing after the render.
    const QString wav2 = tempWavPath("equal-fallback");
    const QJsonValue rvDefault = mcpValue("render_and_verify", QJsonObject{{"outputPath", wav2}});
    ASSERT_TRUE(rvDefault.isObject()) << "a plan-less default must answer, not refuse";
    const QJsonValue mvDefault = rpcPayload("audio.mixVerdict",
                                            QJsonObject{{"filePath", wav2}});
    EXPECT_EQ(QString::fromUtf8(QJsonDocument(mvDefault.toObject()).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument(rvDefault.toObject().value("verdict").toObject())
                                    .toJson(QJsonDocument::Compact)));

    const QString wav3 = tempWavPath("equal-fallback-true");
    const QJsonValue rvTrue = mcpValue("render_and_verify",
                                       QJsonObject{{"outputPath", wav3}, {"fromPlan", true}});
    ASSERT_TRUE(rvTrue.isObject()) << "an explicit plan-less fromPlan:true must fall back, not refuse";
    const QJsonValue mvFalse = rpcPayload("audio.mixVerdict",
                                          QJsonObject{{"filePath", wav3}, {"fromPlan", false}});
    EXPECT_EQ(QString::fromUtf8(QJsonDocument(mvFalse.toObject()).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument(rvTrue.toObject().value("verdict").toObject())
                                    .toJson(QJsonDocument::Compact)));
}

} // namespace
