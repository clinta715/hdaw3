#include <gtest/gtest.h>
#include "mcp/McpServer.h"
#include "common/ToolUnits.h"
#include "engine/AudioEngine.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <memory>
using namespace mcp;

TEST(ToolRegistry, RegisterAndList) {
    McpServer s;
    s.registerTool({"foo","does foo",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    s.registerTool({"bar","does bar",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    EXPECT_EQ(s.tools().size(), 2u);
    EXPECT_TRUE(s.tools().contains("foo"));
}
TEST(ToolRegistry, UnknownToolReturnsToolError) {
    McpServer s;
    s.registerTool({"foo","x",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","baz"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}
TEST(ToolRegistry, InvalidParamsReturnsToolError) {
    McpServer s;
    QJsonObject schema{{"type","object"},{"required", QJsonArray{"x"}},
                       {"properties", QJsonObject{{"x", QJsonObject{{"type","integer"}}}}}};
    s.registerTool({"t","x",schema,"test",[](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","t"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
    EXPECT_TRUE(r.toObject().value("content").toArray().at(0).toObject()
                .value("text").toString().contains("invalid params"));
}
TEST(ToolRegistry, HandlerExceptionBecomesToolError) {
    McpServer s;
    s.registerTool({"boom","x",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&) -> McpToolResult { throw std::runtime_error("nope"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","boom"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}

#include "mcp/McpTools_Private.h"

TEST(ToolRegistry, FxPresetFileToolMentionsSerumPreset) {
    McpServer s;
    registerFxPresetTools(s, nullptr);
    ASSERT_TRUE(s.tools().contains("load_plugin_preset_file"));
    const auto def = s.tools().value("load_plugin_preset_file");
    EXPECT_TRUE(def.description.contains(".SerumPreset"));
    EXPECT_TRUE(def.inputSchema.value("properties").toObject().contains("filePath"));
}
TEST(ToolRegistry, TogglePluginEditorToolMentionsEditor) {
    McpServer s;
    registerFxSlotTools(s, nullptr);
    ASSERT_TRUE(s.tools().contains("toggle_plugin_editor"));
    const auto def = s.tools().value("toggle_plugin_editor");
    EXPECT_TRUE(def.description.contains("editor"));
}

// ---------------------------------------------------------------------------
// Slice S1 schema gates (docs/plans/2026-09-28-agent-mechanization.md):
// central unit annotation + one example per tool. These build the FULL registry
// (same wiring as the frontend parity ratchet test).
// ---------------------------------------------------------------------------

#include "mcp/McpTools.h"

namespace {

// Builds the full 300+-tool registry once per test. mcp:: is spelled out
// because McpTools.h forward-declares a global `class McpServer;`.
struct FullRegistry {
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<AudioEngine> engine;
    FullRegistry() {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }
};

// Walk the STORED (enriched) schema and collect every numeric field whose
// x-unit is missing or "?unclassified".
void collectMissingUnits(const QString& tool, const QJsonObject& props, QStringList& out) {
    for (auto it = props.begin(); it != props.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        const QString type = p.value("type").toString();
        if (type == "number" || type == "integer") {
            const QString u = p.value("x-unit").toString();
            if (u.isEmpty() || u == "?unclassified") out << (tool + "." + it.key());
        } else if (type == "array") {
            const QJsonObject items = p.value("items").toObject();
            const QString ityp = items.value("type").toString();
            if (ityp == "number" || ityp == "integer") {
                const QString u = items.value("x-unit").toString();
                if (u.isEmpty() || u == "?unclassified") out << (tool + "." + it.key() + "[]");
            } else if (ityp == "object") {
                collectMissingUnits(tool, items.value("properties").toObject(), out);
            }
        } else if (type == "object") {
            collectMissingUnits(tool, p.value("properties").toObject(), out);
        }
    }
}

} // namespace

// GATE A (classification coverage): every numeric property in every tool's
// inputSchema must carry a resolved unit (a unit string, or explicit scalar).
TEST(ToolRegistry, SchemaUnitsCoverEveryNumericField) {
    FullRegistry fx;
    ASSERT_GT(fx.server->tools().size(), 200u);

    QStringList offenders;
    for (auto it = fx.server->tools().begin(); it != fx.server->tools().end(); ++it) {
        collectMissingUnits(it.value().name, it.value().inputSchema.value("properties").toObject(), offenders);
    }
    for (const QString& o : offenders)
        ADD_FAILURE() << "numeric field resolves to ?unclassified (no unit): " << o.toStdString();
    EXPECT_TRUE(offenders.isEmpty());
}

// GATE B (examples): the per-tool shape example lives in the STANDARD JSON
// Schema `examples` annotation — an ARRAY inside inputSchema — NOT an ad-hoc
// top-level "example" field a strict MCP client may strip. Every tool carries
// exactly one example object, and when the tool declares `required` props the
// example carries exactly those keys. A ZERO-ARG tool (no properties) carries
// [{}] — always one object, `{}` when it takes no arguments.
TEST(ToolRegistry, ToolsListExamplesLiveInSchema) {
    FullRegistry fx;
    const QJsonArray arr =
        fx.server->handleRequestOnTestThread(1, "tools/list", QJsonObject{}).toObject()
            .value("tools").toArray();
    ASSERT_GT(arr.size(), 200);

    int missing = 0;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        // Clean cutover: the ad-hoc TOP-LEVEL example key must be gone, so a
        // client-stripping regression is visible.
        if (o.contains("example")) {
            ADD_FAILURE() << "top-level ad-hoc example key must be gone: " << name.toStdString();
            ++missing;
        }
        const QJsonObject schema = o.value("inputSchema").toObject();
        const QJsonValue ex = schema.value("examples");
        if (!ex.isArray()) {
            ADD_FAILURE() << "inputSchema.examples must be an array: " << name.toStdString();
            ++missing; continue;
        }
        const QJsonArray exArr = ex.toArray();
        if (exArr.size() != 1 || !exArr.first().isObject()) {
            ADD_FAILURE() << "examples must hold exactly one object: " << name.toStdString();
            ++missing; continue;
        }
        const QJsonArray required = schema.value("required").toArray();
        if (!required.isEmpty()) {
            QStringList exKeys = exArr.first().toObject().keys();
            QStringList reqKeys;
            for (const QJsonValue& r : required) reqKeys << r.toString();
            exKeys.sort();
            reqKeys.sort();
            EXPECT_EQ(exKeys, reqKeys) << "example keys != required keys for " << name.toStdString();
        }
    }
    EXPECT_EQ(missing, 0);
}

// GATE C (no-clobber): an existing property description keeps its original text
// verbatim AND gains the unit sentence (generate_rhythm_pattern.bars).
TEST(ToolRegistry, UnitTextAppendsToExistingDescription) {
    FullRegistry fx;
    const auto def = fx.server->tools().value("generate_rhythm_pattern");
    const QJsonObject bars = def.inputSchema.value("properties").toObject().value("bars").toObject();
    EXPECT_TRUE(bars.value("description").toString().contains("Number of bars (not seconds)"))
        << bars.value("description").toString().toStdString();
    EXPECT_TRUE(bars.value("description").toString().contains("In bars (4/4)."))
        << bars.value("description").toString().toStdString();
    EXPECT_EQ(bars.value("x-unit").toString(), QString("bars"));
}

// GATE D: tools/list must report exactly the registry's tools (no drop, no
// duplicate). Lower-bound guards against accidental mass deregistration.
TEST(ToolRegistry, ToolsListMatchesRegistryCount) {
    FullRegistry fx;
    const QJsonArray arr =
        fx.server->handleRequestOnTestThread(1, "tools/list", QJsonObject{}).toObject()
            .value("tools").toArray();
    ASSERT_GT(arr.size(), 200);
    EXPECT_EQ(static_cast<int>(arr.size()), static_cast<int>(fx.server->tools().size()));
}

// tool_help (S1b): the ONE-CALL form of a tool's tools/list entry. It must be
// the listed entry EXACTLY — same description / inputSchema (units + the
// `examples` array included) — because both read the SAME stored McpToolDef
// (the schema, examples included, was enriched once at registration), so drift
// is impossible by construction and this pins it.
TEST(ToolRegistry, ToolHelpReturnsTheExactToolsListEntry) {
    FullRegistry fx;
    const QJsonArray arr =
        fx.server->handleRequestOnTestThread(1, "tools/list", QJsonObject{}).toObject()
            .value("tools").toArray();

    QJsonObject listed;
    for (const QJsonValue& v : arr)
        if (v.toObject().value("name").toString() == "set_clip") { listed = v.toObject(); break; }
    ASSERT_FALSE(listed.isEmpty()) << "set_clip must be listed";

    const auto res = fx.server->handleRequestOnTestThread(
                         2, "tools/call",
                         QJsonObject{{"name", "tool_help"},
                                     {"arguments", QJsonObject{{"name", "set_clip"}}}})
                         .toObject();
    ASSERT_FALSE(res.value("isError").toBool())
        << res.value("content").toArray().at(0).toObject().value("text").toString().toStdString();
    const QString text = res.value("content").toArray().at(0).toObject().value("text").toString();
    const QJsonObject help = QJsonDocument::fromJson(text.toUtf8()).object();
    ASSERT_FALSE(help.isEmpty()) << text.toStdString();

    EXPECT_EQ(help.value("name").toString(), QString("set_clip"));
    EXPECT_EQ(help.value("description"), listed.value("description"));
    EXPECT_EQ(help.value("category"), listed.value("category"));
    EXPECT_EQ(help.value("inputSchema"), listed.value("inputSchema"));

    // ITEM B clean cutover: NO ad-hoc top-level `example` on EITHER the listed
    // entry or the help entry; the standard annotation is inputSchema.examples,
    // an array of exactly one object.
    EXPECT_FALSE(help.contains("example"))
        << "tool_help must not carry the ad-hoc top-level example key";
    EXPECT_FALSE(listed.contains("example"))
        << "tools/list must not carry the ad-hoc top-level example key";
    const QJsonValue exs = help.value("inputSchema").toObject().value("examples");
    ASSERT_TRUE(exs.isArray()) << "inputSchema.examples must be an array";
    ASSERT_EQ(exs.toArray().size(), 1);
    EXPECT_TRUE(exs.toArray().first().isObject());

    // The unit annotations are part of the stored schema, hence of the help.
    bool sawUnit = false;
    const QJsonObject props = help.value("inputSchema").toObject().value("properties").toObject();
    for (auto it = props.begin(); it != props.end(); ++it)
        if (it.value().toObject().contains("x-unit")) sawUnit = true;
    EXPECT_TRUE(sawUnit) << "tool_help's schema must carry the x-unit annotations";
}

// An unknown tool name is refused in-band with the ONE shared text.
TEST(ToolRegistry, ToolHelpUnknownNameIsRefused) {
    FullRegistry fx;
    const auto res = fx.server->handleRequestOnTestThread(
                         1, "tools/call",
                         QJsonObject{{"name", "tool_help"},
                                     {"arguments", QJsonObject{{"name", "nope"}}}})
                         .toObject();
    EXPECT_TRUE(res.value("isError").toBool());
    EXPECT_EQ(res.value("content").toArray().at(0).toObject().value("text").toString(),
              QString("unknown tool nope"));
}

// ---------------------------------------------------------------------------
// GATE E (PROVEN classification, S1 rev 2): every unit-bearing numeric field on
// the live tools/list must resolve to the RIGHT unit, not merely to *a* unit.
//
// kExpectedUnits is the hand-audited ground truth -- one entry per unit-bearing
// field (unit != scalar) plus every ambiguous scalar name. The handler/engine
// file:line proof for each is carried in src/common/ToolUnits.h above the rule
// that classifies it. This ledger is deliberately redundant with the tables:
// any drift in either direction fails here.
//
//   (a) each entry resolves to its expected x-unit;
//   (b) completeness -- every live numeric field whose x-unit is a real unit
//       MUST have an entry here (a new unit-bearing field cannot silently
//       inherit a guessed label; ADD one entry with its proof);
//   (c) each "scalar" entry is genuinely dimensionless (resolves to "") AND the
//       live schema marks it scalar -- this is the audit conclusion for the
//       ambiguous names (add_fx.position scalar vs seek.position seconds, ...);
//   (d) each entry still exists on the live surface (no stale ledger rows).
// ---------------------------------------------------------------------------
namespace {
struct ExpectedUnit { const char* tool; const char* field; const char* unit; };

// Spot cases with the same field name but different units are all present:
//   export_audio.start=seconds  vs set_clip.start=beats
//   seek.position=seconds       vs add_fx.position=scalar
//   add_arranger_region.duration=seconds vs add_note.duration=beats
//   list_notes.startGte=beats; set_clip.fadeIn=seconds;
//   add_automation_point.time=beats; slice_clip_at_times.times=beats.
const ExpectedUnit kExpectedUnits[] = {
    {"add_arranger_region", "duration", "seconds"},
    {"add_arranger_region", "startTime", "seconds"},
    {"add_audio_clip", "length", "beats"},
    {"add_audio_clip", "start", "beats"},
    {"add_automation_point", "time", "beats"},
    {"add_automation_point", "value", "scalar"},
    {"add_cc_point", "beat", "beats"},
    {"add_fx", "position", "scalar"},
    {"add_instrument_part", "lengthBeats", "beats"},
    {"add_instrument_part", "noteDuration", "beats"},
    {"add_instrument_part", "startBeat", "beats"},
    {"add_instrument_part", "targetRms", "scalar"},
    {"add_instrument_part", "windowSeconds", "seconds"},
    {"add_midi_clip", "length", "beats"},
    {"add_midi_clip", "start", "beats"},
    {"add_midi_fx", "position", "scalar"},
    {"add_note", "duration", "beats"},
    {"add_note", "start", "beats"},
    {"add_note", "velocity", "scalar"},
    {"add_notes", "duration", "beats"},
    {"add_notes", "start", "beats"},
    {"add_notes", "velocity", "scalar"},
    {"add_send", "level", "scalar"},
    {"add_tempo_point", "bpm", "bpm"},
    {"add_tempo_point", "timeSeconds", "seconds"},
    {"apply_movement_plan", "end", "beats"},
    {"apply_movement_plan", "start", "beats"},
    {"audition_patch", "root", "scalar"},
    {"audition_plugin", "lengthBeats", "beats"},
    {"audition_plugin", "noteDuration", "beats"},
    {"audition_plugin", "windowSeconds", "seconds"},
    {"auto_gain_to_target", "targetRms", "scalar"},
    {"auto_gain_to_target", "windowSeconds", "seconds"},
    {"auto_gain_tracks", "targetRms", "scalar"},
    {"auto_gain_tracks", "windowSeconds", "seconds"},
    {"automation_preset", "cycles", "scalar"},
    {"automation_preset", "end", "beats"},
    {"automation_preset", "midPoint", "beats"},
    {"automation_preset", "start", "beats"},
    {"cluster_library", "k", "scalar"},
    {"create_section", "endBeat", "beats"},
    {"create_section", "startBeat", "beats"},
    {"diagnose_intro_blast", "binSeconds", "seconds"},
    {"diagnose_intro_blast", "windowSeconds", "seconds"},
    {"duplicate_clip", "start", "beats"},
    {"duplicate_region", "endBeat", "beats"},
    {"duplicate_region", "startBeat", "beats"},
    {"export_audio", "end", "seconds"},
    {"export_audio", "sampleRate", "Hz"},
    {"export_audio", "start", "seconds"},
    {"export_audio", "waitTimeoutMs", "ms"},
    {"generate_arrangement", "bars", "bars"},
    {"generate_arrangement", "complexity", "scalar"},
    {"generate_arrangement", "swingPercent", "percent"},
    {"generate_arrangement", "velocityMax", "scalar"},
    {"generate_arrangement", "velocityMin", "scalar"},
    {"generate_arrangement_corpus", "bars", "bars"},
    {"generate_automation_envelope", "cycles", "scalar"},
    {"generate_automation_envelope", "density", "scalar"},
    {"generate_automation_envelope", "end", "beats"},
    {"generate_automation_envelope", "phase", "scalar"},
    {"generate_automation_envelope", "smooth", "scalar"},
    {"generate_automation_envelope", "start", "beats"},
    {"generate_automation_envelope", "steps", "scalar"},
    {"generate_chopped_break", "bars", "bars"},
    {"generate_chord", "arpeggioRate", "beats"},
    {"generate_chord", "length", "beats"},
    {"generate_chord", "start", "beats"},
    {"generate_clip_cc_lane", "cycles", "scalar"},
    {"generate_clip_cc_lane", "density", "scalar"},
    {"generate_clip_cc_lane", "end", "beats"},
    {"generate_clip_cc_lane", "phase", "scalar"},
    {"generate_clip_cc_lane", "smooth", "scalar"},
    {"generate_clip_cc_lane", "start", "beats"},
    {"generate_clip_cc_lane", "steps", "scalar"},
    {"generate_clip_gain_envelope", "cycles", "scalar"},
    {"generate_clip_gain_envelope", "density", "scalar"},
    {"generate_clip_gain_envelope", "end", "beats"},
    {"generate_clip_gain_envelope", "phase", "scalar"},
    {"generate_clip_gain_envelope", "smooth", "scalar"},
    {"generate_clip_gain_envelope", "start", "beats"},
    {"generate_clip_gain_envelope", "steps", "scalar"},
    {"generate_phrase", "length", "beats"},
    {"generate_phrase", "noteDuration", "beats"},
    {"generate_phrase", "start", "beats"},
    {"generate_progression", "arpeggioRate", "beats"},
    {"generate_progression", "beatsPerChord", "beats"},
    {"generate_progression", "durationBeats", "beats"},
    {"generate_progression", "start", "beats"},
    {"generate_psytrance", "end", "beats"},
    {"generate_psytrance", "start", "beats"},
    {"generate_psytrance_markov", "bars", "bars"},
    {"generate_psytrance_markov", "everyBars", "bars"},
    {"generate_psytrance_markov", "keyShiftDegrees", "scalar"},
    {"generate_psytrance_markov", "sectionCycleBars", "bars"},
    {"generate_psytrance_markov", "totalBars", "bars"},
    {"generate_rhythm_pattern", "bars", "bars"},
    {"generate_rhythm_pattern", "start", "beats"},
    {"get_waveform_peaks", "numBins", "scalar"},
    {"import_audio_file", "startBeat", "beats"},
    {"insert_silence", "endBeat", "beats"},
    {"insert_silence", "startBeat", "beats"},
    {"key_check", "root", "scalar"},
    {"list_device_params", "limit", "scalar"},
    {"list_notes", "startGte", "beats"},
    {"list_notes", "startLt", "beats"},
    {"loop_clip", "repetitions", "scalar"},
    {"mix_diff", "bpm", "bpm"},
    {"mix_diff", "end", "seconds"},
    {"mix_diff", "start", "seconds"},
    {"mix_report", "bpm", "bpm"},
    {"mix_report", "dropBuildRatio", "scalar"},
    {"mix_report", "end", "seconds"},
    {"mix_report", "start", "seconds"},
    {"mix_verdict", "bpm", "bpm"},
    {"mix_verdict", "dropBuildRatio", "scalar"},
    {"mix_verdict", "end", "seconds"},
    {"mix_verdict", "introSeconds", "seconds"},
    {"mix_verdict", "start", "seconds"},
    {"move_clip", "start", "beats"},
    {"param_verity", "startBeat", "beats"},
    {"param_verity", "windowSeconds", "seconds"},
    {"param_verity_corpus", "startBeat", "beats"},
    {"param_verity_corpus", "windowSeconds", "seconds"},
    {"place_patterns", "durationBeats", "beats"},
    {"place_patterns", "frequency", "scalar"},
    {"place_patterns", "lengthBars", "bars"},
    {"place_patterns", "start", "beats"},
    {"place_patterns", "startBar", "bars"},
    {"place_patterns", "startBeat", "beats"},
    {"preview_set_project_bpm", "bpm", "bpm"},
    {"preview_set_tempo_match", "fileBpm", "bpm"},
    {"preview_set_volume", "volume", "scalar"},
    {"psy_fm_set_mod_route", "depth", "scalar"},
    {"query_clips", "endBeat", "beats"},
    {"query_clips", "startBeat", "beats"},
    {"query_notes", "endBeat", "beats"},
    {"query_notes", "startBeat", "beats"},
    {"rave_import_result", "sourceOffsetBeats", "beats"},
    {"rave_import_result", "startBeats", "beats"},
    {"rave_set_config", "timeoutMs", "ms"},
    {"rave_start_training", "sampleRate", "Hz"},
    {"rave_start_transform", "temperature", "scalar"},
    {"rave_transform_clip", "sourceOffsetBeats", "beats"},
    {"rave_transform_clip", "startBeats", "beats"},
    {"rave_transform_clip", "temperature", "scalar"},
    {"redo", "count", "scalar"},
    {"related_samples", "limit", "scalar"},
    {"remove_notes", "startGte", "beats"},
    {"remove_notes", "startLt", "beats"},
    {"ripple_delete", "endBeat", "beats"},
    {"ripple_delete", "startBeat", "beats"},
    {"search_library", "bpmMax", "bpm"},
    {"search_library", "bpmMin", "bpm"},
    {"search_library", "durationMax", "seconds"},
    {"search_library", "durationMin", "seconds"},
    {"search_library", "limit", "scalar"},
    {"search_library", "offset", "scalar"},
    {"search_plugin_presets", "limit", "scalar"},
    {"seek", "position", "seconds"},
    {"set_arranger_region_bounds", "duration", "seconds"},
    {"set_arranger_region_bounds", "startTime", "seconds"},
    {"set_audio_buffer_size", "size", "scalar"},
    {"set_audio_sample_rate", "rate", "Hz"},
    {"set_automation_points", "time", "beats"},
    {"set_automation_points", "value", "scalar"},
    {"set_bus_fx_param", "value", "scalar"},
    {"set_cc_point", "beat", "beats"},
    {"set_cc_point", "value", "scalar"},
    {"set_clip", "duration", "beats"},
    {"set_clip", "fadeIn", "seconds"},
    {"set_clip", "fadeOut", "seconds"},
    {"set_clip", "gain", "scalar"},
    {"set_clip", "start", "beats"},
    {"set_clip_source_bpm", "bpm", "bpm"},
    {"set_clip_stretch_mode", "mode", "scalar"},
    {"set_clip_stretch_ratio", "ratio", "scalar"},
    // S3 batch verbs: set_clips_edit's `edits[]` item fields (nested object
    // properties -> keys are tool.field) and set_notes_gain's gain.
    {"set_clips_edit", "duration", "beats"},
    {"set_clips_edit", "fadeIn", "seconds"},
    {"set_clips_edit", "fadeOut", "seconds"},
    {"set_clips_edit", "gain", "scalar"},
    {"set_clips_edit", "start", "beats"},
    {"set_notes_gain", "gain", "scalar"},
    {"set_fx_param", "value", "scalar"},
    {"set_internal_fx_param", "value", "scalar"},
    {"set_lfo_param", "value", "scalar"},
    {"set_master_gain", "gain", "scalar"},
    {"set_midi_fx_param", "value", "scalar"},
    {"set_note", "duration", "beats"},
    {"set_note", "start", "beats"},
    {"set_note", "velocity", "scalar"},
    {"set_note_chance", "chance", "scalar"},
    {"set_note_gain", "gain", "scalar"},
    {"set_note_occurrence", "occurrence", "scalar"},
    {"set_note_pan", "pan", "scalar"},
    {"set_note_pitch_offset", "pitchOffset", "semitones"},
    {"set_note_pressure", "pressure", "scalar"},
    {"set_note_recurrence", "recurrence", "scalar"},
    {"set_note_repeat_count", "repeatCount", "scalar"},
    {"set_note_repeat_curve", "repeatCurve", "scalar"},
    {"set_note_repeat_rate", "repeatRate", "beats"},
    {"set_note_timbre", "timbre", "scalar"},
    {"set_note_velocities", "startGte", "beats"},
    {"set_note_velocities", "startLt", "beats"},
    {"set_note_velocities", "velocity", "scalar"},
    {"set_sampler_key_range", "keyHigh", "scalar"},
    {"set_sampler_key_range", "keyLow", "scalar"},
    {"set_scale", "mode", "scalar"},
    {"set_scale", "root", "scalar"},
    {"set_song_plan", "bars", "bars"},
    {"set_song_plan", "bpm", "bpm"},
    {"set_song_plan", "totalBars", "bars"},
    {"set_tempo", "bpm", "bpm"},
    {"set_tempo_point_bpm", "bpm", "bpm"},
    {"set_tempo_point_time", "timeSeconds", "seconds"},
    {"set_time_signature", "denominator", "scalar"},
    {"set_time_signature", "numerator", "scalar"},
    {"set_track", "height", "scalar"},
    {"set_track", "midiChannel", "scalar"},
    {"set_track", "pan", "scalar"},
    {"set_track", "volume", "scalar"},
    {"set_track_send_level", "level", "scalar"},
    {"setup_remix", "bpm", "bpm"},
    {"setup_remix", "sectionLengthBeats", "beats"},
    {"slice_clip_at_times", "times", "beats"},
    {"tone_verity", "attackMsMax", "ms"},
    {"tone_verity", "attackMsMin", "ms"},
    {"tone_verity", "binSeconds", "seconds"},
    {"tone_verity", "f0CentsMax", "cents"},
    {"tone_verity", "f0Hz", "Hz"},
    {"tone_verity", "modRateHz", "Hz"},
    {"tone_verity", "modRateTolPct", "percent"},
    {"tone_verity", "startBeat", "beats"},
    {"tone_verity", "windowSeconds", "seconds"},
    {"transport", "loopEnd", "seconds"},
    {"transport", "loopStart", "seconds"},
    {"undo", "count", "scalar"},
    {"verify_part", "endBeat", "beats"},
    {"verify_part", "startBeat", "beats"},
    {"verify_part", "windowSeconds", "seconds"},
    // S4 (docs/plans/2026-09-28-agent-mechanization.md §5): verify_window speaks
    // BEATS at the tool boundary (converted at the project BPM by
    // AudioEngineCommands::verifyWindow — AudioEngineCommands_Composition.cpp:1614-1615),
    // and both render-launching tools bound their wait with the same millisecond
    // budget as export_audio.waitTimeoutMs.
    {"verify_window", "startBeat", "beats"},
    {"verify_window", "endBeat", "beats"},
    {"verify_window", "timeoutMs", "ms"},
    {"render_and_verify", "timeoutMs", "ms"},
    // ITEM 4: render_and_verify gained mix_verdict's knobs, so its verdict equals
    // mix_verdict's for the produced file. introSeconds is the same seconds-bounded
    // intro-blast window as mix_verdict.introSeconds; dropBuildRatio is the
    // dimensionless loudness floor.
    {"render_and_verify", "introSeconds", "seconds"},
    {"render_and_verify", "dropBuildRatio", "scalar"},

    // S6 (docs/plans/2026-09-28-agent-mechanization.md §4): unit-explicit time
    // windows. Every new spelling is declared by the ONE spec table
    // (src/common/WindowUnitArgs.h) and injected into the stored schema at
    // registration, so it is unit-bearing on the live surface and must be listed
    // here. `*Sec` resolves to seconds, `*Beat/*Beats` to beats, `unit` is a string.
    {"automation_preset", "startSec", "seconds"},
    {"automation_preset", "endSec", "seconds"},
    {"automation_preset", "startBeat", "beats"},
    {"automation_preset", "endBeat", "beats"},
    {"apply_movement_plan", "startSec", "seconds"},
    {"apply_movement_plan", "endSec", "seconds"},
    {"apply_movement_plan", "startBeat", "beats"},
    {"apply_movement_plan", "endBeat", "beats"},
    {"generate_automation_envelope", "startSec", "seconds"},
    {"generate_automation_envelope", "endSec", "seconds"},
    {"generate_automation_envelope", "startBeat", "beats"},
    {"generate_automation_envelope", "endBeat", "beats"},
    {"generate_clip_gain_envelope", "startSec", "seconds"},
    {"generate_clip_gain_envelope", "endSec", "seconds"},
    {"generate_clip_gain_envelope", "startBeat", "beats"},
    {"generate_clip_gain_envelope", "endBeat", "beats"},
    {"generate_clip_cc_lane", "startSec", "seconds"},
    {"generate_clip_cc_lane", "endSec", "seconds"},
    {"generate_clip_cc_lane", "startBeat", "beats"},
    {"generate_clip_cc_lane", "endBeat", "beats"},
    {"export_audio", "startBeat", "beats"},
    {"export_audio", "endBeat", "beats"},
    {"export_audio", "startSec", "seconds"},
    {"export_audio", "endSec", "seconds"},
    {"verify_window", "startSec", "seconds"},
    {"verify_window", "endSec", "seconds"},
    {"verify_part", "startSec", "seconds"},
    {"verify_part", "endSec", "seconds"},
    {"query_notes", "startSec", "seconds"},
    {"query_notes", "endSec", "seconds"},
    {"query_clips", "startSec", "seconds"},
    {"query_clips", "endSec", "seconds"},
    {"create_section", "startSec", "seconds"},
    {"create_section", "endSec", "seconds"},
    {"duplicate_region", "startSec", "seconds"},
    {"duplicate_region", "endSec", "seconds"},
    {"ripple_delete", "startSec", "seconds"},
    {"ripple_delete", "endSec", "seconds"},
    {"insert_silence", "startSec", "seconds"},
    {"insert_silence", "endSec", "seconds"},
    {"param_verity", "startSec", "seconds"},
    {"param_verity_corpus", "startSec", "seconds"},
    {"tone_verity", "startSec", "seconds"},
    {"place_patterns", "startSec", "seconds"},
    {"import_audio_file", "startSec", "seconds"},
    {"add_instrument_part", "startSec", "seconds"},
    {"add_midi_clip", "startSec", "seconds"},
    {"add_midi_clip", "lengthSec", "seconds"},
    {"add_midi_clip", "startBeat", "beats"},
    {"add_midi_clip", "lengthBeat", "beats"},
    {"add_audio_clip", "startSec", "seconds"},
    {"add_audio_clip", "lengthSec", "seconds"},
    {"add_audio_clip", "startBeat", "beats"},
    {"add_audio_clip", "lengthBeat", "beats"},
    {"set_clip", "startSec", "seconds"},
    {"set_clip", "durationSec", "seconds"},
    {"set_clip", "startBeat", "beats"},
    {"set_clip", "durationBeat", "beats"},
    {"set_clip", "fadeInBeat", "beats"},
    {"set_clip", "fadeOutBeat", "beats"},
    {"set_clip", "fadeInSec", "seconds"},
    {"set_clip", "fadeOutSec", "seconds"},
    {"move_clip", "startSec", "seconds"},
    {"move_clip", "startBeat", "beats"},
    {"duplicate_clip", "startSec", "seconds"},
    {"duplicate_clip", "startBeat", "beats"},
    {"add_note", "startSec", "seconds"},
    {"add_note", "durationSec", "seconds"},
    {"add_note", "startBeat", "beats"},
    {"add_note", "durationBeat", "beats"},
    {"add_notes", "startSec", "seconds"},
    {"add_notes", "durationSec", "seconds"},
    {"add_notes", "startBeat", "beats"},
    {"add_notes", "durationBeat", "beats"},
    {"set_note", "startSec", "seconds"},
    {"set_note", "durationSec", "seconds"},
    {"set_note", "startBeat", "beats"},
    {"set_note", "durationBeat", "beats"},
    {"generate_chord", "startSec", "seconds"},
    {"generate_chord", "lengthSec", "seconds"},
    {"generate_chord", "startBeat", "beats"},
    {"generate_chord", "lengthBeat", "beats"},
    {"generate_phrase", "startSec", "seconds"},
    {"generate_phrase", "lengthSec", "seconds"},
    {"generate_phrase", "startBeat", "beats"},
    {"generate_phrase", "lengthBeat", "beats"},
    {"generate_progression", "startSec", "seconds"},
    {"generate_progression", "startBeat", "beats"},
    {"generate_rhythm_pattern", "startSec", "seconds"},
    {"generate_rhythm_pattern", "startBeat", "beats"},
    {"generate_psytrance", "startSec", "seconds"},
    {"generate_psytrance", "endSec", "seconds"},
    {"generate_psytrance", "startBeat", "beats"},
    {"generate_psytrance", "endBeat", "beats"},
    {"add_automation_point", "timeSec", "seconds"},
    {"add_automation_point", "timeBeat", "beats"},
    {"set_automation_points", "timeSec", "seconds"},
    {"set_automation_points", "timeBeat", "beats"},
    {"slice_clip_at_times", "timesSec", "seconds"},
    {"add_cc_point", "beatSec", "seconds"},
    {"set_cc_point", "beatSec", "seconds"},
    {"audition_plugin", "noteDurationSec", "seconds"},
    {"list_notes", "startGteSec", "seconds"},
    {"list_notes", "startLtSec", "seconds"},
    {"remove_notes", "startGteSec", "seconds"},
    {"remove_notes", "startLtSec", "seconds"},
    {"set_note_velocities", "startGteSec", "seconds"},
    {"set_note_velocities", "startLtSec", "seconds"},
    {"mix_report", "startBeat", "beats"},
    {"mix_report", "endBeat", "beats"},
    {"mix_report", "startSec", "seconds"},
    {"mix_report", "endSec", "seconds"},
    {"mix_verdict", "startBeat", "beats"},
    {"mix_verdict", "endBeat", "beats"},
    {"mix_verdict", "startSec", "seconds"},
    {"mix_verdict", "endSec", "seconds"},
    {"mix_diff", "startBeat", "beats"},
    {"mix_diff", "endBeat", "beats"},
    {"mix_diff", "startSec", "seconds"},
    {"mix_diff", "endSec", "seconds"},
    {"add_arranger_region", "startTimeBeat", "beats"},
    {"add_arranger_region", "durationBeat", "beats"},
    {"set_arranger_region_bounds", "startTimeBeat", "beats"},
    {"set_arranger_region_bounds", "durationBeat", "beats"},
    {"audition_plugin", "noteDurationBeat", "beats"},
    {"list_notes", "startGteBeat", "beats"},
    {"list_notes", "startLtBeat", "beats"},
    {"remove_notes", "startGteBeat", "beats"},
    {"remove_notes", "startLtBeat", "beats"},
    {"set_note_velocities", "startGteBeat", "beats"},
    {"set_note_velocities", "startLtBeat", "beats"},
    {"add_arranger_region", "startTimeBeat", "beats"},
    {"add_arranger_region", "durationBeat", "beats"},
    {"add_arranger_region", "startTimeSec", "seconds"},
    {"add_arranger_region", "durationSec", "seconds"},
    {"set_arranger_region_bounds", "startTimeBeat", "beats"},
    {"set_arranger_region_bounds", "durationBeat", "beats"},
    {"set_arranger_region_bounds", "startTimeSec", "seconds"},
    {"set_arranger_region_bounds", "durationSec", "seconds"},
    {"transport", "loopStartBeat", "beats"},
    {"transport", "loopEndBeat", "beats"},
    {"transport", "loopStartSec", "seconds"},
    {"transport", "loopEndSec", "seconds"},
    {"seek", "positionBeat", "beats"},
    {"seek", "positionSec", "seconds"},
};

// Walk the ENRICHED live schemas and map "tool.field" -> x-unit (numeric
// properties, numeric array items and nested object properties).
void collectLiveUnits(const QString& tool, const QJsonObject& props,
                      QHash<QString, QString>& out) {
    for (auto it = props.begin(); it != props.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        const QString type = p.value("type").toString();
        const QString key = tool + "." + it.key();
        if (type == "number" || type == "integer") {
            out.insert(key, p.value("x-unit").toString());
        } else if (type == "array") {
            const QJsonObject items = p.value("items").toObject();
            const QString ityp = items.value("type").toString();
            if (ityp == "number" || ityp == "integer")
                out.insert(key, items.value("x-unit").toString());
            else if (ityp == "object")
                collectLiveUnits(tool, items.value("properties").toObject(), out);
        } else if (type == "object") {
            collectLiveUnits(tool, p.value("properties").toObject(), out);
        }
    }
}
} // namespace

TEST(ToolRegistry, SchemaUnitsAreProvenClassified) {
    FullRegistry fx;
    ASSERT_GT(fx.server->tools().size(), 200u);

    QHash<QString, QString> live;
    for (auto it = fx.server->tools().begin(); it != fx.server->tools().end(); ++it)
        collectLiveUnits(it.value().name,
                         it.value().inputSchema.value("properties").toObject(), live);

    QHash<QString, QString> expected;
    for (const ExpectedUnit& e : kExpectedUnits)
        expected.insert(QString(e.tool) + "." + e.field, QString(e.unit));

    // (a) + (c) + (d)
    for (const ExpectedUnit& e : kExpectedUnits) {
        const QString key = QString(e.tool) + "." + e.field;
        const QString want = QString(e.unit);
        const QString got = toolunits::resolveFieldUnit(e.tool, e.field);
        // resolveFieldUnit returns "" for a classified scalar; the schema renders
        // that as x-unit "scalar".
        const QString gotLabel = got.isEmpty() ? QString("scalar") : got;
        EXPECT_EQ(gotLabel, want) << "wrong x-unit for " << key.toStdString();
        if (!live.contains(key)) { ADD_FAILURE() << "EXPECTED row not on live surface: " << key.toStdString(); continue; }
        const QString liveUnit = live.value(key);
        if (want == "scalar") {
            // (c) the audit conclusion: a scalar entry is dimensionless.
            EXPECT_TRUE(got.isEmpty()) << key.toStdString() << " classified scalar but resolves to a unit";
            EXPECT_EQ(liveUnit, QString("scalar")) << key.toStdString() << " not marked scalar in tools/list";
        } else {
            EXPECT_EQ(liveUnit, want) << "live x-unit drifted for " << key.toStdString();
        }
    }

    // (b) completeness: no unit-bearing live field outside the ledger.
    int missing = 0;
    for (auto it = live.begin(); it != live.end(); ++it) {
        const QString u = it.value();
        if (u.isEmpty() || u == "scalar" || u == "?unclassified") continue;
        if (!expected.contains(it.key())) {
            ADD_FAILURE() << "unit-bearing field missing from kExpectedUnits: " << it.key().toStdString()
                          << " (x-unit=" << u.toStdString() << ")";
            ++missing;
        }
    }
    EXPECT_EQ(missing, 0);
}

// ---------------------------------------------------------------------------
// P4-c (2026-09-30): an enum refusal names the offending value AND the allowed
// set, and the allowed set is byte-sourced from the SAME schema the validator
// used — a handler-copied list cannot drift.
// ---------------------------------------------------------------------------
namespace {

QString enumRefusalText(const QJsonObject& args) {
    FullRegistry fx;
    const auto r = fx.server->handleRequestOnTestThread(
        1, "tools/call", QJsonObject{ { "name", "set_cell" }, { "arguments", args } });
    return r.toObject().value("content").toArray().at(0).toObject().value("text").toString();
}

} // namespace

TEST(ToolRegistry, EnumRefusalNamesValueAndAllowedSet) {
    const QString text = enumRefusalText(QJsonObject{
        { "section", "intro" }, { "role", "bass" }, { "trackId", 0 }, { "source", "bogus" } });
    EXPECT_EQ(text,
        QString("invalid params: source: value \"bogus\" not in enum "
                "(allowed: \"phrase\", \"rhythm\", \"break\", \"pattern\", \"harvest\")"))
        << text.toStdString();
}

TEST(ToolRegistry, EnumRefusalAllowedSetMatchesSchemaExactly) {
    FullRegistry fx;
    const auto schemaEnum = fx.server->tools().value("set_cell").inputSchema
        .value("properties").toObject().value("source").toObject().value("enum").toArray();
    ASSERT_GT(schemaEnum.size(), 0);

    QString allowed;
    for (const auto& ev : schemaEnum) {
        if (!allowed.isEmpty()) allowed += ", ";
        allowed += "\"" + ev.toString() + "\"";
    }
    const QString text = enumRefusalText(QJsonObject{
        { "section", "intro" }, { "role", "bass" }, { "trackId", 0 }, { "source", "bogus2" } });
    EXPECT_TRUE(text.contains("value \"bogus2\" not in enum (allowed: " + allowed + ")"))
        << text.toStdString();
}
