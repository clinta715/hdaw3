// psy_fm preset + algorithm expansion (slice D of
// docs/plans/2026-10-05-internal-synth-expansion.md).
//
// ADDITIVE: two algorithms are APPENDED (index 4 pad = dual carrier, index 5
// bell = deep feedback chain) and five role presets are APPENDED to
// PsyFmState's preset table (pad/bell/pluck/drone/stab). Existing preset names,
// algorithm indices 0..3, and their renders are UNCHANGED — the back-compat
// tests below pin FNV-1a hashes of the four existing presets plus a default
// slot, captured by building the engine with slice D REVERTED (the four
// presetTable() rows and the two algorithm switches restored to their
// pre-slice-D form; slices A/B/C present). Any DSP edit or preset-table
// reorder moves one of these.
//
// Slice E (2026-10-06): PresetDef gained `filter[5]` (params 33..37) and
// setFxSlotPsyFmPreset writes it, so a preset is the WHOLE sound. The four
// original rows carry the NEUTRAL filter {20000, 0.7, 0, 0, 0} — the def
// defaults and slice B's bypass condition — so those same D goldens still
// hold (asserted again below).
#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/PsyFmAlgorithms.h"
#include "engine/PsyFmEngine.h"
#include "engine/PsyFmPatches.h"
#include "engine/PsyFmState.h"
#include "engine/TrackFXSlot.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools_Private.h"
#include "model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace HDAW;

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 512;

// ── The five rows slice D appended, in table order ──
const char* const kNewPresets[] = { "pad", "bell", "pluck", "drone", "stab" };
const char* const kOldPresets[] = { "growlBass", "acidLead", "metallicPluck", "riser" };

uint32_t hashSamples (const std::vector<float>& x)
{
    uint32_t h = 2166136261u;
    for (float v : x)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        h ^= bits;
        h *= 16777619u;
    }
    return h;
}

double peakOf (const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x) p = std::max (p, (double) std::fabs (v));
    return p;
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v)) return false;
    return true;
}

juce::ValueTree treeFxChain (AudioEngine& engine, int trackIndex)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return {};
    return trackList.getChild (trackIndex).getChildWithName (IDs::FX_CHAIN);
}

juce::ValueTree treeFxSlot (AudioEngine& engine, int trackIndex, int slotIndex)
{
    auto chain = treeFxChain (engine, trackIndex);
    if (! chain.isValid() || slotIndex < 0 || slotIndex >= chain.getNumChildren())
        return {};
    return chain.getChild (slotIndex);
}

// ── Deterministic render of ONE slot tree through a real TrackFXSlot ──
// Same shape as the hash capture: prepare, then load the persisted params and
// the persisted matrix/sweep (the rebuild path), then a held chord for 64
// blocks, both channels collected.
std::vector<float> renderSlotTree (const juce::ValueTree& slotTree, int blocks = 64,
                                   const std::vector<int>& notes = { 36, 43, 48 })
{
    TrackFXSlot slot ("psy_fm");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);
    if (slotTree.isValid())
    {
        slot.loadParamsFromTree (slotTree);
        slot.loadPsyFmStateFromTree (slotTree);
    }

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        buffer.clear();
        slot.process (buffer, midi);
        midi.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            out.insert (out.end(), p, p + kBlockSize);
        }
    }
    return out;
}

// ── Spectral fingerprint: 3-band energy fractions + spectral centroid ──
// A cascade of 3 one-poles per band (18 dB/oct) makes the bands cleanly
// separable; the centroid comes from a Hann-windowed DFT of a decimated frame
// (decimation keeps the O(N^2) DFT cheap without aliasing the centroid's
// dominant region for these mostly-low material signals).
struct Spectrum
{
    double low = 0.0, mid = 0.0, high = 0.0;   // energy fractions, sum == 1
    double centroidHz = 0.0;
};

struct OnePoleLP
{
    double a = 0.0, y = 0.0;
    void setFc (double fc) { a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc / kSampleRate); }
    double process (double x) { y += a * (x - y); return y; }
};
struct OnePoleHP
{
    double a = 0.0, y = 0.0, xp = 0.0;
    void setFc (double fc) { a = std::exp (-2.0 * juce::MathConstants<double>::pi * fc / kSampleRate); }
    double process (double x) { y = a * (y + x - xp); xp = x; return y; }
};

double bandEnergy (const std::vector<float>& x, double fc, bool highpass)
{
    OnePoleLP lp0, lp1, lp2;
    OnePoleHP hp0, hp1, hp2;
    if (highpass) { hp0.setFc (fc); hp1.setFc (fc); hp2.setFc (fc); }
    else          { lp0.setFc (fc); lp1.setFc (fc); lp2.setFc (fc); }
    double e = 0.0;
    for (float v : x)
    {
        double y;
        if (highpass)
        {
            y = hp0.process ((double) v);
            y = hp1.process (y);
            y = hp2.process (y);
        }
        else
        {
            y = lp0.process ((double) v);
            y = lp1.process (y);
            y = lp2.process (y);
        }
        e += y * y;
    }
    return e;
}

Spectrum spectrumOf (const std::vector<float>& x, double sampleRate)
{
    Spectrum s;

    // Bands at 300 Hz and 3 kHz (cascaded one-poles, 18 dB/oct).
    const double lowE  = bandEnergy (x, 300.0, false);
    const double above300 = bandEnergy (x, 300.0, true);
    const double highE = bandEnergy (x, 3000.0, true);
    // mid = energy that is above 300 but below 3000 == (above300 - highE),
    // clamped >= 0 for the one-pole cascade's small overshoot.
    const double midE = std::max (0.0, above300 - highE);
    const double total = lowE + midE + highE;
    if (total > 0.0)
    {
        s.low  = lowE  / total;
        s.mid  = midE  / total;
        s.high = highE / total;
    }

    // Spectral centroid over the MAX-ENERGY window of the render. A fixed
    // offset cannot work for both a 0.1 s pluck and a 2 s-attack drone: the
    // percussive presets are silent by 1/3 in, so picking the loudest window
    // measures the character where the preset actually sounds. Decimated DFT
    // (keeps the O(N^2) transform cheap; decimation does not alias the
    // dominant low region of these band-limited signals).
    const int decim = 8;
    const int N = 512;
    const size_t win = (size_t) N * decim;
    if (x.size() >= win)
    {
        size_t bestBegin = 0;
        double bestRms = -1.0;
        const size_t stride = std::max<size_t> (1, x.size() / 16);
        for (size_t begin = 0; begin + win <= x.size(); begin += stride)
        {
            double e = 0.0;
            for (size_t i = 0; i < win; ++i) e += (double) x[begin + i] * (double) x[begin + i];
            if (e > bestRms) { bestRms = e; bestBegin = begin; }
        }

        std::vector<double> frame;
        frame.reserve ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const size_t idx = bestBegin + (size_t) i * decim;
            if (idx >= x.size()) break;
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (N - 1));
            frame.push_back ((double) x[idx] * w);
        }

        if (frame.size() >= 32)
        {
            const double fSample = sampleRate / decim;
            double num = 0.0, den = 0.0;
            for (int k = 1; k < (int) frame.size() / 2; ++k)
            {
                double re = 0.0, im = 0.0;
                for (int n = 0; n < (int) frame.size(); ++n)
                {
                    const double ang = 2.0 * juce::MathConstants<double>::pi * k * n / (double) frame.size();
                    re += frame[(size_t) n] * std::cos (ang);
                    im -= frame[(size_t) n] * std::sin (ang);
                }
                const double mag = std::sqrt (re * re + im * im);
                const double f = k * fSample / (double) frame.size();
                num += f * mag;
                den += mag;
            }
            if (den > 0.0) s.centroidHz = num / den;
        }
    }
    return s;
}

// L1 distance between two band-fraction vectors.
double bandDistance (const Spectrum& a, const Spectrum& b)
{
    return std::fabs (a.low - b.low) + std::fabs (a.mid - b.mid) + std::fabs (a.high - b.high);
}

std::string spectrumString (const Spectrum& s)
{
    char buf[160];
    std::snprintf (buf, sizeof (buf),
                   "low=%.3f mid=%.3f high=%.3f centroid=%.0fHz",
                   s.low, s.mid, s.high, s.centroidHz);
    return buf;
}

// ── Fixture: one track + one psy_fm slot through the command layer (the same
// path both surfaces use), plus the MCP server and the RPC dispatcher. ──
struct PsyFmPresetFixture
{
    void setUp()
    {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        engine->getProjectCommands().addTrack ("Presets");
        engine->getProjectCommands().addFxSlot (0, "psy_fm", 0, "");
        engine->getProjectCommands().addFxSlot (0, "psy_fm", 1, "");
        engine->drainPendingRoutingRebuild();
        server.setEngine (engine.get());
        mcp::registerPsyFmTools (server, engine.get());
        // Slice E: get_internal_fx_param is the read-back surface the preset
        // filter round-trip is verified against.
        mcp::registerFxSlotTools (server, engine.get());
    }

    // get_internal_fx_param's params array for a slot (real units).
    QJsonArray readParamsViaMcp (int slotIndex)
    {
        const auto r = server.handleRequestOnTestThread (
            2, "tools/call",
            QJsonObject { { "name", "get_internal_fx_param" },
                          { "arguments", QJsonObject { { "trackId", 0 },
                                                       { "slotIndex", slotIndex } } } }).toObject();
        const bool isErr = r.value ("isError").toBool();
        EXPECT_FALSE (isErr) << "get_internal_fx_param failed";
        if (isErr) return {};
        const QString text = r.value ("content").toArray().at (0).toObject()
                                 .value ("text").toString();
        return QJsonDocument::fromJson (text.toUtf8()).object()
                   .value ("params").toArray();
    }

    // Load `preset` into slot `slotIndex` through the MCP tool.
    bool loadViaMcp (int slotIndex, const QString& preset)
    {
        const auto r = server.handleRequestOnTestThread (
            1, "tools/call",
            QJsonObject { { "name", "psy_fm_load_preset" },
                          { "arguments", QJsonObject { { "trackId", 0 },
                                                       { "slotIndex", slotIndex },
                                                       { "preset", preset } } } }).toObject();
        return ! r.value ("isError").toBool();
    }

    // Load `preset` into slot `slotIndex` through the JSON-RPC twin.
    bool loadViaRpc (int slotIndex, const QString& preset)
    {
        const auto r = frontend::dispatch (
            *engine, "psy_fm.loadPreset",
            QJsonObject { { "trackIndex", 0 }, { "slotIndex", slotIndex },
                          { "preset", preset } });
        return ! r.isError;
    }

    // The preset payload of a slot tree — exactly what the load path writes:
    // param_0..N, psyFmMatrix, psyFmSweepRate. Identity/chain properties
    // (slotID, position, bypassed) are excluded so two independently created
    // slots can be compared on preset content alone. A canonical
    // std::map<std::string,std::string> is the comparison channel (juce::String
    // and QString do not implicitly convert, and the map prints on failure).
    static std::map<std::string, std::string> slotParams (const juce::ValueTree& slot)
    {
        std::map<std::string, std::string> o;
        for (int i = 0; i < slot.getNumProperties(); ++i)
        {
            const std::string name = slot.getPropertyName (i).toString().toStdString();
            if (! (name.rfind ("param_", 0) == 0 || name == "psyFmMatrix"
                   || name == "psyFmSweepRate"))
                continue;
            const auto v = slot.getProperty (slot.getPropertyName (i));
            if (v.isDouble())
            {
                char buf[64];
                std::snprintf (buf, sizeof (buf), "%.9g", (double) v);
                o[name] = buf;
            }
            else
            {
                o[name] = v.toString().toStdString();
            }
        }
        return o;
    }

    bool loadPresetIntoSlot (int slotIndex, const std::string& name)
    {
        return engine->getAudioEngineCommands().setFxSlotPsyFmPreset (0, slotIndex, name);
    }

    std::unique_ptr<AudioEngine> engine;
    mcp::McpServer server;
};

} // namespace

// ============================================================================
// Back-compat: existing presets + a default slot render hash-identically
// ============================================================================
// Goldens captured from the engine with slice D REVERTED (2026-10-06); the
// four rows and both algorithm switches were restored to their pre-slice-D
// form. FNV-1a over the raw float bits of both channels.

TEST (PsyFmPresetBackCompat, ExistingFourPresetsRenderIdenticalToPreSliceD)
{
    struct Golden { const char* name; uint32_t hash; };
    const Golden goldens[] = {
        { "growlBass",     0xcad4acfdu },
        { "acidLead",      0x9e5b9db9u },
        { "metallicPluck", 0xe7c245e9u },
        { "riser",         0xe52b8255u },
    };

    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack ("T");
    engine.getProjectCommands().addFxSlot (0, "psy_fm", 0, "");
    engine.drainPendingRoutingRebuild();

    for (const auto& g : goldens)
    {
        ASSERT_TRUE (engine.getAudioEngineCommands().setFxSlotPsyFmPreset (0, 0, g.name))
            << g.name;
        const auto out = renderSlotTree (treeFxSlot (engine, 0, 0));
        ASSERT_TRUE (allFinite (out));
        ASSERT_GT (peakOf (out), 0.01) << g.name << " must be non-silent";
        EXPECT_EQ (hashSamples (out), g.hash)
            << g.name << ": existing preset render moved; computed 0x"
            << std::hex << hashSamples (out);
    }
}

TEST (PsyFmPresetBackCompat, DefaultSlotRendersIdenticalToPreSliceD)
{
    // 0xba3c4b19 == the pre-slice-D default-slot hash (captured the same way;
    // it is also slice B's pinned slot-default hash, so the neutral path is
    // unchanged across both slices).
    constexpr uint32_t kGoldenHash = 0xba3c4b19u;

    const auto out = renderSlotTree (juce::ValueTree {});
    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01);
    EXPECT_EQ (hashSamples (out), kGoldenHash)
        << "default psy_fm slot render moved; computed 0x" << std::hex << hashSamples (out);
}

// ============================================================================
// The new rows load byte-identically on BOTH surfaces
// ============================================================================

TEST (PsyFmPresetSurfaces, NewPresetsLoadIdenticallyViaToolAndRpc)
{
    PsyFmPresetFixture f;
    f.setUp();

    for (const char* name : kNewPresets)
    {
        const QString q (name);
        ASSERT_TRUE (f.loadViaMcp (0, q)) << name << ": MCP tool refused a table name";
        ASSERT_TRUE (f.loadViaRpc (1, q)) << name << ": RPC twin refused a table name";
    }

    // Slot 0 was written by the tool, slot 1 by the RPC twin — for the LAST
    // preset each carries, the persisted trees must be byte-identical.
    const auto a = PsyFmPresetFixture::slotParams (treeFxSlot (*f.engine, 0, 0));
    const auto b = PsyFmPresetFixture::slotParams (treeFxSlot (*f.engine, 0, 1));
    EXPECT_EQ (a, b) << "tool and RPC twin wrote different slot state";

    // And a replay of one preset through both must match its table row.
    for (int i = 0; i < (int) (sizeof (kNewPresets) / sizeof (kNewPresets[0])); ++i)
    {
        ASSERT_TRUE (f.loadPresetIntoSlot (0, kNewPresets[i]));
        ASSERT_TRUE (f.loadPresetIntoSlot (1, kNewPresets[i]));
        const auto pa = PsyFmPresetFixture::slotParams (treeFxSlot (*f.engine, 0, 0));
        const auto pb = PsyFmPresetFixture::slotParams (treeFxSlot (*f.engine, 0, 1));
        EXPECT_EQ (pa, pb) << kNewPresets[i];
    }
}

TEST (PsyFmPresetSurfaces, UnknownPresetRefusalNamesTheWholeSetOnBothSurfaces)
{
    PsyFmPresetFixture f;
    f.setUp();

    const auto rpc = frontend::dispatch (
        *f.engine, "psy_fm.loadPreset",
        QJsonObject { { "trackIndex", 0 }, { "slotIndex", 0 }, { "preset", "nope" } });
    ASSERT_TRUE (rpc.isError);
    const QString rpcMsg = rpc.payload.toObject().value ("message").toString();
    EXPECT_EQ (rpcMsg.toStdString(), PsyFmState::unknownPresetError ("nope"));
    for (const char* name : { "pad", "bell", "pluck", "drone", "stab" })
        EXPECT_TRUE (rpcMsg.contains (name)) << name << ": " << rpcMsg.toStdString();

    const auto mcp = f.server.handleRequestOnTestThread (
        1, "tools/call",
        QJsonObject { { "name", "psy_fm_load_preset" },
                      { "arguments", QJsonObject { { "trackId", 0 }, { "slotIndex", 0 },
                                                   { "preset", "nope" } } } }).toObject();
    ASSERT_TRUE (mcp.value ("isError").toBool());
    const QString mcpMsg = mcp.value ("content").toArray().at (0).toObject()
                               .value ("text").toString();
    for (const char* name : { "pad", "bell", "pluck", "drone", "stab" })
        EXPECT_TRUE (mcpMsg.contains (name)) << name << ": " << mcpMsg.toStdString();
}

// ============================================================================
// Every new preset is non-silent AND spectrally distinct from the others
// ============================================================================

TEST (PsyFmPresetSpectrum, NewPresetsAreNonSilentAndMutuallyDistinct)
{
    PsyFmPresetFixture f;
    f.setUp();

    // Long enough for `drone`'s 2 s attack to reach its sustain (the plan's
    // "very slow" intent), so every preset contributes its steady-state energy.
    constexpr int kBlocks = 320;   // ~3.7 s

    std::vector<Spectrum> spectra;
    for (const char* name : kNewPresets)
    {
        ASSERT_TRUE (f.loadPresetIntoSlot (0, name)) << name;
        const auto out = renderSlotTree (treeFxSlot (*f.engine, 0, 0), kBlocks, { 60, 64 });
        ASSERT_TRUE (allFinite (out)) << name;
        ASSERT_GT (peakOf (out), 0.005) << name << " must render non-silent";

        const Spectrum s = spectrumOf (out, kSampleRate);
        spectra.push_back (s);
        std::cout << "[psyfm-spectrum] " << name << ": " << spectrumString (s) << "\n";
        EXPECT_GE (s.low + s.mid + s.high, 0.99) << name << ": bands must sum to 1";
        EXPECT_GT (s.centroidHz, 0.0) << name;
    }

    // MEASURED distinctness: every ordered pair must differ in either the band
    // profile (L1 > 0.05) or the centroid (ratio > 1.05).
    for (size_t i = 0; i < spectra.size(); ++i)
        for (size_t j = i + 1; j < spectra.size(); ++j)
        {
            const double band = bandDistance (spectra[i], spectra[j]);
            const double cHi = std::max (spectra[i].centroidHz, spectra[j].centroidHz);
            const double cLo = std::min (spectra[i].centroidHz, spectra[j].centroidHz);
            const double ratio = (cLo > 0.0) ? cHi / cLo : 1.0;
            EXPECT_TRUE (band > 0.05 || ratio > 1.05)
                << kNewPresets[i] << " vs " << kNewPresets[j]
                << " are not spectrally distinct: bandL1=" << band
                << " centroidRatio=" << ratio;
        }
}

TEST (PsyFmPresetSpectrum, NewPresetAlgorithmsChangeTheRender)
{
    // Algorithm 4 (pad) and 5 (bell) must be REAL routings, not aliases of an
    // existing one: the same ratios/envs rendered through each algorithm differ.
    auto renderAlgo = [] (int algo, const float ratios[6], float feedback,
                          const juce::ADSR::Parameters& env, float output)
    {
        PsyFmEngine engine;
        switch (algo)
        {
            case 1: engine.setAlgorithm (acidLeadAlgorithm); break;
            case 2: engine.setAlgorithm (metallicPluckAlgorithm); break;
            case 3: engine.setAlgorithm (riserAlgorithm); break;
            case 4: engine.setAlgorithm (padAlgorithm); break;
            case 5: engine.setAlgorithm (bellAlgorithm); break;
            default: engine.setAlgorithm (growlBassAlgorithm); break;
        }
        engine.setBaseRatios (ratios);
        engine.setBaseFeedback (feedback);
        for (int op = 0; op < 6; ++op) engine.setOpEnvelope (op, env);
        engine.setOutputLevel (output);
        engine.prepare (kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 48; ++b)
        {
            midi.clear();
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            engine.render (buffer, midi);
            const auto* p = buffer.getReadPointer (0);
            out.insert (out.end(), p, p + kBlockSize);
        }
        return out;
    };

    const float ratios[6] = { 1.0f, 1.005f, 1.0f, 2.0f, 1.0f, 1.0f };
    const juce::ADSR::Parameters env { 0.01f, 0.4f, 0.8f, 0.3f };

    const auto pad  = renderAlgo (4, ratios, 0.05f, env, 0.7f);
    const auto bell = renderAlgo (5, ratios, 0.05f, env, 0.7f);
    const auto growl = renderAlgo (0, ratios, 0.05f, env, 0.7f);

    ASSERT_GT (peakOf (pad), 0.01);
    ASSERT_GT (peakOf (bell), 0.01);
    EXPECT_NE (hashSamples (pad), hashSamples (bell));
    EXPECT_NE (hashSamples (pad), hashSamples (growl));
    EXPECT_NE (hashSamples (bell), hashSamples (growl));
}

// ============================================================================
// Preset table + algorithm wiring integrity
// ============================================================================

TEST (PsyFmPresetTable, NewRowsArePresentAndConsistent)
{
    for (const char* name : kNewPresets)
    {
        const auto* p = PsyFmState::findPreset (name);
        ASSERT_NE (p, nullptr) << name;
        EXPECT_EQ (std::string (p->name), name);
        // 0..5 are all valid now; a new preset may reuse an existing routing
        // (pluck/stab do) — what matters is that the value is in range.
        EXPECT_GE (p->algorithm, 0) << name;
        EXPECT_LE (p->algorithm, 5) << name;
        EXPECT_GE (p->feedback, 0.0f);
        EXPECT_LE (p->feedback, 1.0f);
        EXPECT_GE (p->outputLevel, 0.0f);
        EXPECT_LE (p->outputLevel, 1.0f);
        // Envelopes are inside the slot def ranges (attack<=2, decay<=5,
        // sustain 0..1, release<=5) so the load path never has to clamp.
        for (int op = 0; op < 6; ++op)
        {
            EXPECT_GE (p->env[op][0], 0.001f) << name;
            EXPECT_LE (p->env[op][0], 2.0f) << name;
            EXPECT_LE (p->env[op][1], 5.0f) << name;
            EXPECT_GE (p->env[op][2], 0.0f);
            EXPECT_LE (p->env[op][2], 1.0f);
            EXPECT_LE (p->env[op][3], 5.0f) << name;
        }
        auto routes = PsyFmState::decodeRoutes (p->matrix);
        EXPECT_FALSE (routes.empty()) << name << " must carry a decoded matrix";
    }

    // The vocabulary grew by exactly the five appended names, and both legacy
    // and new names are enumerable.
    const auto names = PsyFmState::presetNames();
    ASSERT_EQ (names.size(), 9u);
    for (const char* n : kOldPresets) EXPECT_TRUE (PsyFmState::isKnownPreset (n)) << n;
    for (const char* n : kNewPresets) EXPECT_TRUE (PsyFmState::isKnownPreset (n)) << n;
}

TEST (PsyFmPresetTable, ExistingRowsAreUnchanged)
{
    // Pin the four pre-slice-D rows' algorithm + matrix so a reorder cannot
    // silently repoint an existing name (the hash tests above cover the DSP).
    struct Row { const char* name; int algorithm; const char* matrix; };
    const Row rows[] = {
        { "growlBass",     0, "feedbackLFO:op6Feedback:0.4" },
        { "acidLead",      1, "modWheel:op6Feedback:0.9" },
        { "metallicPluck", 2, "feedbackLFO:op6Feedback:0.15" },
        { "riser",         3, "barClock:ratioSweepRate:1;ratioSweepLFO:op4Ratio:0.3" },
    };
    for (const auto& r : rows)
    {
        const auto* p = PsyFmState::findPreset (r.name);
        ASSERT_NE (p, nullptr) << r.name;
        EXPECT_EQ (p->algorithm, r.algorithm) << r.name;
        EXPECT_EQ (std::string (p->matrix), std::string (r.matrix)) << r.name;
    }
}

TEST (PsyFmPresetTable, PatchHelpersMatchTheEncodedRows)
{
    // PsyFmPatches::make*Matrix() and the preset rows' `matrix` strings are two
    // spellings of the same routing; this pins them together.
    struct Pair { const char* preset; PsyFmModMatrix (*fn) (); };
    const Pair pairs[] = {
        { "pad",   &PsyFmPatches::makePadMatrix },
        { "bell",  &PsyFmPatches::makeBellMatrix },
        { "pluck", &PsyFmPatches::makePluckMatrix },
        { "drone", &PsyFmPatches::makeDroneMatrix },
        { "stab",  &PsyFmPatches::makeStabMatrix },
    };
    for (const auto& pr : pairs)
    {
        const auto* row = PsyFmState::findPreset (pr.preset);
        ASSERT_NE (row, nullptr) << pr.preset;
        const PsyFmModMatrix m = pr.fn();
        EXPECT_EQ (PsyFmState::encodeRoutes (m.getRoutes()), std::string (row->matrix))
            << pr.preset;
    }
}

TEST (PsyFmPresetTable, Param32RangeCoversEveryAlgorithm)
{
    TrackFXSlot slot ("psy_fm");
    const auto defs = slot.getInternalParamDefs();
    ASSERT_GT ((int) defs.size(), 32);
    EXPECT_EQ (defs[32].name, juce::String ("Algorithm Preset"));
    EXPECT_NEAR (defs[32].minValue, 0.0f, 1e-6f);
    EXPECT_NEAR (defs[32].maxValue, 5.0f, 1e-6f);

    // Both switch sites accept 4 and 5: set the param before prepare AND live,
    // and assert the LIVE engine's algorithm changed the render vs 0.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;

    auto renderSlotWithAlgo = [&] (int algo, bool beforePrepare)
    {
        TrackFXSlot s ("psy_fm");
        s.setInternalParam (31, 1.0f);
        if (beforePrepare) s.setInternalParam (32, (float) algo);
        s.prepare (spec);
        if (! beforePrepare) s.setInternalParam (32, (float) algo);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 32; ++b)
        {
            midi.clear();
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            buffer.clear();
            s.process (buffer, midi);
            midi.clear();
            const auto* p = buffer.getReadPointer (0);
            out.insert (out.end(), p, p + kBlockSize);
        }
        return out;
    };

    for (int algo : { 4, 5 })
    {
        const auto pre  = renderSlotWithAlgo (algo, true);
        const auto live = renderSlotWithAlgo (algo, false);
        const auto base = renderSlotWithAlgo (0, true);
        ASSERT_GT (peakOf (pre), 0.005) << algo;
        ASSERT_GT (peakOf (live), 0.005) << algo;
        EXPECT_NE (hashSamples (pre), hashSamples (base))
            << "algo " << algo << " (prepare site) must change the render";
        EXPECT_NE (hashSamples (live), hashSamples (base))
            << "algo " << algo << " (live site) must change the render";
    }
}

// ============================================================================
// Slice E: the preset carries the post-carrier filter (params 33..37)
// ============================================================================
// Before slice E, setFxSlotPsyFmPreset wrote params 0..32 + matrix, so every
// preset loaded with the filter at its neutral defaults. Slice E makes the
// preset the WHOLE sound: the table rows now carry `filter[5]` and the command
// writes 33..37 in the same undo unit. The four pre-slice-D rows keep the
// NEUTRAL filter, so their renders (the D goldens above) are unchanged.

TEST (PsyFmPresetFilter, OldRowsCarryTheNeutralFilter)
{
    // 20000/0.7/0/0/0 == the def defaults AND slice B's bypass condition
    // (cutoff<19999 || keyTrack!=0 || envAmount!=0), so writing it is a
    // render no-op — that is what keeps the D hashes above valid.
    for (const char* name : kOldPresets)
    {
        const auto* p = PsyFmState::findPreset (name);
        ASSERT_NE (p, nullptr) << name;
        EXPECT_FLOAT_EQ (p->filter[0], 20000.0f) << name;
        EXPECT_FLOAT_EQ (p->filter[1], 0.7f) << name;
        EXPECT_FLOAT_EQ (p->filter[2], 0.0f) << name;
        EXPECT_FLOAT_EQ (p->filter[3], 0.0f) << name;
        EXPECT_FLOAT_EQ (p->filter[4], 0.0f) << name;
    }
}

TEST (PsyFmPresetFilter, LoadWritesEveryNewRowsFilterParams)
{
    // The load path must write 33..37 for real: read back through
    // get_internal_fx_param (the ReadModel real-unit surface) and compare to
    // the table row — catches a dropped write, a wrong index, or a clamp.
    struct Row { const char* name; float f[5]; };
    const Row expected[] = {
        { "pad",   { 1200.0f, 0.7f, 0.0f, 0.3f, 0.4f } },
        { "bell",  { 6000.0f, 1.2f, 0.0f, 1.0f, 0.0f } },
        { "pluck", { 10000.0f, 1.0f, 0.0f, 0.5f, 0.7f } },
        { "drone", {  800.0f, 0.5f, 0.0f, 0.0f, 0.0f } },
        { "stab",  {  700.0f, 1.5f, 0.0f, 0.3f, 0.4f } },
    };

    PsyFmPresetFixture f;
    f.setUp();

    for (const auto& r : expected)
    {
        const auto* row = PsyFmState::findPreset (r.name);
        ASSERT_NE (row, nullptr) << r.name;
        // The table row and this test's expectation cannot drift.
        for (int i = 0; i < 5; ++i)
            EXPECT_FLOAT_EQ (row->filter[i], r.f[i]) << r.name << " row filter[" << i << "]";

        ASSERT_TRUE (f.loadPresetIntoSlot (0, r.name)) << r.name;
        const QJsonArray params = f.readParamsViaMcp (0);
        ASSERT_EQ (params.size(), 38) << r.name;
        for (int i = 0; i < 5; ++i)
        {
            const QJsonObject p = params.at (33 + i).toObject();
            EXPECT_EQ (p.value ("index").toInt(), 33 + i) << r.name;
            EXPECT_NEAR (p.value ("value").toDouble(), (double) r.f[i], 1e-3)
                << r.name << " param_" << (33 + i) << " (" << p.value ("name").toString().toStdString() << ")";
        }

        // The RPC read surface (read.getInternalFxParams) reports the same
        // values — the preset-carried filter round-trips on BOTH surfaces.
        const auto rpc = frontend::dispatch (
            *f.engine, "read.getInternalFxParams",
            QJsonObject { { "trackIndex", 0 }, { "slotIndex", 0 } });
        ASSERT_FALSE (rpc.isError) << r.name;
        const QJsonArray rpcParams = rpc.payload.toArray();
        ASSERT_EQ (rpcParams.size(), 38) << r.name;
        for (int i = 0; i < 5; ++i)
        {
            const QJsonObject p = rpcParams.at (33 + i).toObject();
            EXPECT_EQ (p.value ("paramIndex").toInt(), 33 + i) << r.name;
            EXPECT_NEAR (p.value ("value").toDouble(), (double) r.f[i], 1e-3)
                << r.name << " RPC param_" << (33 + i);
        }
    }
}

TEST (PsyFmPresetFilter, FilterIsBypassedForTheOldRowsAfterLoad)
{
    // The old rows' neutral filter must leave the SLOT's own live filter at
    // the neutral defaults (proves the write is exactly the neutral set, not
    // merely "close enough").
    const auto* growl = PsyFmState::findPreset ("growlBass");
    ASSERT_NE (growl, nullptr);

    PsyFmPresetFixture f;
    f.setUp();
    ASSERT_TRUE (f.loadPresetIntoSlot (0, "growlBass"));
    const QJsonArray params = f.readParamsViaMcp (0);
    ASSERT_EQ (params.size(), 38);
    EXPECT_DOUBLE_EQ (params.at (33).toObject().value ("value").toDouble(), 20000.0);
    EXPECT_NEAR (params.at (34).toObject().value ("value").toDouble(), 0.7, 1e-5);
    EXPECT_DOUBLE_EQ (params.at (35).toObject().value ("value").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ (params.at (36).toObject().value ("value").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ (params.at (37).toObject().value ("value").toDouble(), 0.0);
}

TEST (PsyFmPresetFilter, NewPresetRendersDifferentlyFromTheSamePresetWithANeutralFilter)
{
    // The slice-E acceptance gate: a new preset's render must differ from the
    // SAME preset with its filter neutralised — proof the filter actually took
    // effect in DSP (not just in the tree).
    struct Row { const char* name; float f[5]; };

    for (const auto& r : std::vector<Row> {
             { "pad",   { 1200.0f, 0.7f, 0.0f, 0.3f, 0.4f } },
             { "bell",  { 6000.0f, 1.2f, 0.0f, 1.0f, 0.0f } },
             { "pluck", { 10000.0f, 1.0f, 0.0f, 0.5f, 0.7f } },
             { "drone", {  800.0f, 0.5f, 0.0f, 0.0f, 0.0f } },
             { "stab",  {  700.0f, 1.5f, 0.0f, 0.3f, 0.4f } } })
    {
        const auto* row = PsyFmState::findPreset (r.name);
        ASSERT_NE (row, nullptr) << r.name;

        // Render the preset AS LOADED (filter on) ...
        juce::ValueTree on (juce::Identifier ("FX_SLOT"));
        on.setProperty (juce::Identifier ("fxType"), juce::String ("psy_fm"), nullptr);
        for (int i = 0; i < 6; ++i)
            on.setProperty (juce::Identifier ("param_" + juce::String (i)),
                            (double) row->ratios[i], nullptr);
        on.setProperty (juce::Identifier ("param_6"), (double) row->feedback, nullptr);
        for (int op = 0; op < 6; ++op)
            for (int k = 0; k < 4; ++k)
                on.setProperty (juce::Identifier ("param_" + juce::String (7 + op * 4 + k)),
                                (double) row->env[op][k], nullptr);
        on.setProperty (juce::Identifier ("param_31"), (double) row->outputLevel, nullptr);
        on.setProperty (juce::Identifier ("param_32"), (double) row->algorithm, nullptr);
        on.setProperty (juce::Identifier ("psyFmMatrix"), juce::String (row->matrix), nullptr);
        on.setProperty (juce::Identifier ("psyFmSweepRate"), (double) row->sweepRateHz, nullptr);
        for (int i = 0; i < 5; ++i)
            on.setProperty (juce::Identifier ("param_" + juce::String (33 + i)),
                            (double) r.f[i], nullptr);

        // ... and the SAME tree with the filter neutralised.
        auto off = on.createCopy();
        const float neutral[5] = { 20000.0f, 0.7f, 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 5; ++i)
            off.setProperty (juce::Identifier ("param_" + juce::String (33 + i)),
                             (double) neutral[i], nullptr);

        // 320 blocks (~3.7 s): long enough for `drone`'s 2 s attack to reach
        // sustain, matching the spectrum test's proven-loud window.
        const auto outOn  = renderSlotTree (on,  320, { 60, 64, 67 });
        const auto outOff = renderSlotTree (off, 320, { 60, 64, 67 });

        ASSERT_TRUE (allFinite (outOn)) << r.name;
        ASSERT_GT (peakOf (outOn), 0.005) << r.name << " filtered render must be non-silent";
        ASSERT_GT (peakOf (outOff), 0.005) << r.name;
        EXPECT_NE (hashSamples (outOn), hashSamples (outOff))
            << r.name << ": the preset's filter must change the render vs neutral";
    }
}

