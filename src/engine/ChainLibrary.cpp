// src/engine/ChainLibrary.cpp
// File storage for named FX-chain presets. Mirrors src/engine/PatternLibrary.cpp:
// root/user/<sanitized>.json, uniquified with -N, load by relative path,
// scan of *.json. JSON carries "version"; unknown future fields are
// ignored on load (best-effort). No exceptions across API boundaries:
// empty id / empty preset signal failure. All public methods perform
// blocking file IO: message thread (or background/test thread) only,
// never the audio thread.
#include "ChainLibrary.h"
#include "common/DebugLog.h"
#include <algorithm>

namespace HDAW {

namespace {

juce::String sanitizeFileName(const juce::String& name)
{
    juce::String trimmed = name.trim();
    juce::String result;
    result.preallocateBytes((size_t) juce::jmax(16, trimmed.length()));
    for (int i = 0; i < trimmed.length(); ++i)
    {
        juce::juce_wchar c = trimmed[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_';
        result += ok ? juce::String::charToString(c) : juce::String("_");
    }
    if (result.length() > 64)
        result = result.substring(0, 64);
    if (result.isEmpty())
        result = "preset";
    return result;
}

// Forward-tolerant reader: only known fields are read, anything else ignored.
ChainPreset presetFromObject(juce::DynamicObject* obj)
{
    ChainPreset p;
    if (obj == nullptr)
        return p;
    if (obj->hasProperty("version"))
        p.version = (int) obj->getProperty("version");
    p.name = obj->getProperty("name").toString();
    if (auto* slotsVar = obj->getProperty("slots").getArray())
    {
        for (const auto& sv : *slotsVar)
        {
            auto* sObj = sv.getDynamicObject();
            if (sObj == nullptr)
                continue;
            ChainPreset::Slot s;
            s.fxType = sObj->getProperty("fxType").toString();
            s.bypassed = (bool) sObj->getProperty("bypassed");
            s.name = sObj->getProperty("name").toString();
            if (auto* paramsObj = sObj->getProperty("params").getDynamicObject())
            {
                for (const auto& prop : paramsObj->getProperties())
                    s.params[prop.name.toString()] = (double) prop.value;
            }
            if (auto* plugObj = sObj->getProperty("plugin").getDynamicObject())
            {
                s.plugin.id = plugObj->getProperty("id").toString();
                s.plugin.format = plugObj->getProperty("format").toString();
                s.plugin.path = plugObj->getProperty("path").toString();
                s.plugin.stateBase64 = plugObj->getProperty("stateBase64").toString();
            }
            if (auto* sampObj = sObj->getProperty("sampler").getDynamicObject())
            {
                for (const auto& prop : sampObj->getProperties())
                    s.sampler[prop.name.toString()] = prop.value.toString();
            }
            s.slicePoints = sObj->getProperty("slicePoints").toString();
            s.slicePointsOverride = sObj->getProperty("slicePointsOverride").toString();
            s.sliceMeta = sObj->getProperty("sliceMeta").toString();
            s.psyFmMatrix = sObj->getProperty("psyFmMatrix").toString();
            if (sObj->hasProperty("psyFmSweepRate"))
                s.psyFmSweepRate = (double) sObj->getProperty("psyFmSweepRate");
            p.slots.push_back(std::move(s));
        }
    }
    return p;
}

juce::DynamicObject::Ptr presetToObject(const ChainPreset& p)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("version", p.version);
    obj->setProperty("name", p.name);
    juce::Array<juce::var> slotsArr;
    for (const auto& s : p.slots)
    {
        juce::DynamicObject::Ptr sObj = new juce::DynamicObject();
        sObj->setProperty("fxType", s.fxType);
        sObj->setProperty("bypassed", s.bypassed);
        sObj->setProperty("name", s.name);
        juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
        for (const auto& kv : s.params)
            paramsObj->setProperty(kv.first, kv.second);
        sObj->setProperty("params", juce::var(paramsObj.get()));
        juce::DynamicObject::Ptr plugObj = new juce::DynamicObject();
        plugObj->setProperty("id", s.plugin.id);
        plugObj->setProperty("format", s.plugin.format);
        plugObj->setProperty("path", s.plugin.path);
        plugObj->setProperty("stateBase64", s.plugin.stateBase64);
        sObj->setProperty("plugin", juce::var(plugObj.get()));
        juce::DynamicObject::Ptr sampObj = new juce::DynamicObject();
        for (const auto& kv : s.sampler)
            sampObj->setProperty(kv.first, kv.second);
        sObj->setProperty("sampler", juce::var(sampObj.get()));
        sObj->setProperty("slicePoints", s.slicePoints);
        sObj->setProperty("slicePointsOverride", s.slicePointsOverride);
        sObj->setProperty("sliceMeta", s.sliceMeta);
        sObj->setProperty("psyFmMatrix", s.psyFmMatrix);
        sObj->setProperty("psyFmSweepRate", s.psyFmSweepRate);
        slotsArr.add(juce::var(sObj.get()));
    }
    obj->setProperty("slots", slotsArr);
    return obj;
}

bool isFactoryId(const juce::String& id)
{
    return id.startsWith("_factory/") || id.startsWith("_factory\\");
}

// --- Built-in factory rosters (internal FX only) -----------------------------
//
// CHAINS: psytrance per-role chains (one file per entry, several slots each).
// PATCHES: the 12 built-in bass patches (one file per entry, ONE slot each).
//
// Param indices/units mirror TrackFXSlot::getParamDefsForType (real units,
// keyed "param_<index>"): saturator 0=Drive dB 0..40, 1=Type 0..3 (0=SoftTanh,
// 1=SoftAtan, 2=Hard, 3=Bitcrush), 2=Asymmetry -1..1, 3=Mix 0..1,
// 4=Output dB -24..24; eq 0=Frequency 20..20000, 1=Q 0.1..10,
// 2=Gain -24..24; compressor 0=Threshold -80..0, 1=Ratio 1..40,
// 2=Attack 0.1..100, 3=Release 1..2000; reverb 0=Room Size 0..1,
// 1=Damping 0..1, 2=Wet 0..1, 3=Dry 0..1, 4=Width 0..1; chorus 0=Rate,
// 1=Depth, 2=Centre Delay, 3=Feedback, 4=Mix; filter 0=Cutoff 20..20000,
// 1=Mode 0..2 (0=lowpass), 2=Resonance 0.1..10; delay 0=Delay Time s,
// 1=Feedback, 2=Mix, 3=SyncToTempo (1 = time derived from Division + BPM),
// 4=Division enum (1=1/16, 4=dotted-1/8); phaser 0=Rate, 1=Depth,
// 2=Centre Frequency, 3=Feedback, 4=Mix.
//
// The patch roster's instrument slots own their param tables (TrackFXSlot
// derives them from the engines, so the entries below are kept in that order):
// reese_bass 0=Voice Count 1..7, 1=Detune Cents 0..100, 2=Stereo Spread 0..1,
// 3=Osc Shape 0=Saw/1=Square/2=Tri, 4=Phase Scatter 0..1, 5=Sub Level 0..1,
// 6=Sub Octave -2..0, 7=Sync Amount 0..1, 8=Sync Ratio 1..4, 9=Comb Amount
// 0..1, 10=Comb Frequency 20..2000, 11=Comb Feedback 0..0.95, 12=Drive dB
// 0..40, 13=Drive Type 0=Tanh/1=Atan/2=Hard/3=Fold, 14=Drive Mix 0..1,
// 15=Filter Cutoff 20..20000, 16=Filter Res 0.1..20, 17=Filter Type 0/1/2,
// 18=Filter Env Amt 0..1, 19=Filter Key Track 0..1, 20=Filter Attack
// 0.001..2, 21=Filter Decay 0.001..5, 22=Filter Sustain 0..1, 23=Filter
// Release 0.001..5, 24=Amp Attack 0.001..2, 25=Amp Decay 0.001..5, 26=Amp
// Sustain 0..1, 27=Amp Release 0.001..5, 28=Output Level 0..1, 29=LFO Shape
// 0..3, 30=LFO Rate (beats) 0.0625..8, 31=LFO Sync 0/1, 32=LFO Cutoff Amt
// 0..1, 33=LFO Pitch Amt 0..1, 34=LFO Drive Amt 0..1, 35=LFO Phase 0..1,
// 36=Glide 0..2, 37=Mono Legato 0/1, 38=Pitch Bend Range 0..12,
// 39=Velocity->Drive 0..1; growl_bass 0=Fundamental Hz 0..200 (0=follow MIDI
// note), 1=Mod Ratio 0.5..8, 2=Mod Depth 0..1, 3=Mod Shape 0/1/2, 4=Clip Type
// 0/1/2/3, 5=Drive dB 0..40, 6=Asymmetry -1..1, 7=Bitcrush Bits 2..16,
// 8=Filter Cutoff 20..20000, 9=Filter Res 0.1..20, 10=Filter Env Amt 0..1,
// 11=Filter Type 0/1, 12=Attack ms 0.1..100, 13=Decay ms 1..1000,
// 14=Sustain 0..1, 15=Release ms 1..1000, 16=Output Level 0..1,
// 17=Unison Enable 0/1, 18=Unison Voices 1..4, 19=Unison Detune 0..50,
// 20=Ratio Jitter 0/1, 21=Jitter Amount 0..0.5, 22=Formant Enable 0/1,
// 23=Formant Morph 0..1, 24=Sidechain Drive 0/1, 25=Sidechain Amt 0..1;
// sub_synth 0=Osc1 Wave 0..3, 1=Osc1 Level 0..1, 2=Osc2 Wave 0..3,
// 3=Osc2 Level 0..1, 4=Osc2 Detune -1200..1200, 5=Sub Level 0..1,
// 6=Sub Octave -2..0, 7=Cutoff 20..20000, 8=Resonance 0..0.99, 9=Drive 0..1,
// 10=Attack 0.001..5, 11=Decay 0.001..5, 12=Sustain 0..1, 13=Release
// 0.001..5, 14=Output Level 0..1.5, 15=Legato 0/1, 16=Portamento 0..5,
// 17=Filter Type 0..3, 18=Filter Env Amount 0..48, 19=Filter Attack
// 0.001..5, 20=Filter Decay 0.001..5, 21=Filter Sustain 0..1, 22=Filter
// Release 0.001..5, 23=Pitch Bend Range 0..12, 24=Polyphony 0=mono/1=poly.

struct FactorySlotDef {
    const char* fxType;
    const char* name;
    std::vector<std::pair<const char*, double>> params;
};

struct FactoryChainDef {
    const char* name;
    std::vector<FactorySlotDef> slots;
};

// A factory PATCH: ONE slot's full state (a single-slot ChainPreset), the
// slot-scoped sibling of FactoryChainDef. Same FactorySlotDef shape, so the
// slot JSON is built by the SAME factorySlotObject() the chain roster uses.
struct FactoryPatchDef {
    const char* name;
    FactorySlotDef slot;
};

const std::vector<FactoryChainDef>& factoryChainDefs()
{
    static const std::vector<FactoryChainDef> defs = {
        // Mild SoftTanh drive for punch, eq tames sub boom, eq lifts beater click.
        { "Kick Punch", {
            { "saturator", "Kick Drive",
              { { "param_0", 14.0 }, { "param_1", 0.0 }, { "param_3", 0.6 }, { "param_4", -2.0 } } },
            { "eq", "Sub Control",
              { { "param_0", 55.0 }, { "param_1", 1.2 }, { "param_2", -3.0 } } },
            { "eq", "Click Boost",
              { { "param_0", 4000.0 }, { "param_1", 1.0 }, { "param_2", 3.0 } } },
        }},
        // Gentle glue: body eq, slow-attack compressor, whisper of saturation.
        { "Bass Glue", {
            { "eq", "Bass Body",
              { { "param_0", 120.0 }, { "param_1", 0.9 }, { "param_2", 1.5 } } },
            { "compressor", "Glue",
              { { "param_0", -18.0 }, { "param_1", 3.0 }, { "param_2", 12.0 }, { "param_3", 120.0 } } },
            { "saturator", "Warmth",
              { { "param_0", 8.0 }, { "param_1", 0.0 }, { "param_3", 0.5 }, { "param_4", -1.0 } } },
        }},
        // Canon's second filter pass (instrument -> filter -> filter -> shaper)
        // plus a mild atan squash; the closed resonant cutoff is the automation
        // target for the sweep (docs/psytrance-va-and-production.md §5).
        { "Bass Filter Sweep", {
            { "filter", "Second Pass",
              { { "param_0", 500.0 }, { "param_1", 0.0 }, { "param_2", 5.5 } } },
            { "saturator", "Squash",
              { { "param_0", 12.0 }, { "param_1", 1.0 }, { "param_3", 0.35 }, { "param_4", -2.0 } } },
        }},
        // Mid-band grit (principle 2/3) with a lowpass after the shaper so the
        // hard-clipped harmonics stay off the hats.
        { "Bass Mid Growl", {
            { "eq", "Mid Push",
              { { "param_0", 700.0 }, { "param_1", 1.0 }, { "param_2", 3.0 } } },
            { "saturator", "Gnarl",
              { { "param_0", 24.0 }, { "param_1", 2.0 }, { "param_2", 0.2 }, { "param_3", 0.6 }, { "param_4", -3.0 } } },
            { "filter", "Tame Top",
              { { "param_0", 4000.0 }, { "param_1", 0.0 }, { "param_2", 1.2 } } },
        }},
        // The dub throw on a bass stem: every repeat is high-passed (and the
        // send sees a low cut) so the low end stays intact.
        { "Bass Dub Throw", {
            { "eq", "Low Cut",
              { { "param_0", 160.0 }, { "param_1", 0.9 }, { "param_2", -4.0 } } },
            { "delay", "Dotted 8th",
              { { "param_0", 0.321 }, { "param_1", 0.55 }, { "param_2", 0.35 }, { "param_3", 1.0 }, { "param_4", 4.0 } } },
            { "reverb", "Small Room",
              { { "param_0", 0.3 }, { "param_1", 0.4 }, { "param_2", 0.25 }, { "param_3", 0.8 }, { "param_4", 0.8 } } },
        }},
        // Air lift plus a small, bright room that stays out of the way.
        { "Hat Air", {
            { "eq", "Air",
              { { "param_0", 9000.0 }, { "param_1", 0.7 }, { "param_2", 3.0 } } },
            { "reverb", "Short Room",
              { { "param_0", 0.25 }, { "param_1", 0.3 }, { "param_2", 0.30 }, { "param_3", 0.9 }, { "param_4", 1.0 } } },
        }},
        // Wide slow chorus into a large lush hall.
        { "Pad Shimmer", {
            { "chorus", "Shimmer",
              { { "param_0", 0.6 }, { "param_1", 0.45 }, { "param_2", 12.0 }, { "param_3", 0.15 }, { "param_4", 0.5 } } },
            { "reverb", "Lush Hall",
              { { "param_0", 0.92 }, { "param_1", 0.25 }, { "param_2", 0.42 }, { "param_3", 0.7 }, { "param_4", 1.0 } } },
        }},
        // Resonant lowpass squelch; delay tempo-syncs to a dotted 1/8.
        { "Acid Lead", {
            { "filter", "Acid Squelch",
              { { "param_0", 1200.0 }, { "param_1", 0.0 }, { "param_2", 4.5 } } },
            { "delay", "Dotted 8th",
              { { "param_0", 0.321 }, { "param_1", 0.45 }, { "param_2", 0.35 }, { "param_3", 1.0 }, { "param_4", 4.0 } } },
        }},
        // Subtle widening chorus plus a quiet synced 1/16 slap.
        { "Arp Width", {
            { "chorus", "Width",
              { { "param_0", 1.2 }, { "param_1", 0.25 }, { "param_2", 8.0 }, { "param_3", 0.0 }, { "param_4", 0.3 } } },
            { "delay", "16th Slap",
              { { "param_0", 0.107 }, { "param_1", 0.3 }, { "param_2", 0.22 }, { "param_3", 1.0 }, { "param_4", 1.0 } } },
        }},
        // Narrow mid snip keeps stabs out of the lead's way; phaser barely moves.
        { "Stab Snip", {
            { "eq", "Mid Snip",
              { { "param_0", 1800.0 }, { "param_1", 3.5 }, { "param_2", -2.5 } } },
            { "phaser", "Subtle Move",
              { { "param_0", 0.4 }, { "param_1", 0.3 }, { "param_2", 1400.0 }, { "param_3", 0.2 }, { "param_4", 0.3 } } },
        }},
        // Closed resonant filter start (sweep it with an automation lane)
        // into a long bright tail.
        { "Riser Sweep", {
            { "filter", "Sweep Start",
              { { "param_0", 400.0 }, { "param_1", 0.0 }, { "param_2", 6.0 } } },
            { "reverb", "Long Tail",
              { { "param_0", 0.95 }, { "param_1", 0.2 }, { "param_2", 0.5 }, { "param_3", 0.6 }, { "param_4", 1.0 } } },
        }},
    };
    return defs;
}

// The built-in bass PATCH roster: 12 authored single-slot patches covering the
// three bass engines (6 reese_bass, 4 growl_bass, 2 sub_synth). Shipped as
// `_factory/<Name>.json` so every session starts with a bass vocabulary
// (documented gap the factory chains did not fill: a chain is processing, a
// patch is a SOUND). Values are real units per the index map above; each was
// checked against TrackFXSlot::getParamDefsForType by
// ChainLibrary.FactoryPatchesLoadWithinDefRanges.
const std::vector<FactoryPatchDef>& factoryPatchDefs()
{
    static const std::vector<FactoryPatchDef> defs = {
        // ── reese_bass: the detuned-supersaw neuro/wobble voice ──────────────
        // The textbook reese: 7 saws at 28 cents, gentle soft-tanh drive, comb
        // notch, wide-open lowpass; the 1-beat synced LFO is the movement.
        { "Reese Classic", { "reese_bass", "",
            { { "param_0", 7.0 }, { "param_1", 28.0 }, { "param_2", 0.6 }, { "param_3", 0.0 },
              { "param_4", 0.5 }, { "param_5", 0.35 }, { "param_6", -1.0 }, { "param_7", 0.0 },
              { "param_8", 1.0 }, { "param_9", 0.0 }, { "param_10", 80.0 }, { "param_11", 0.7 },
              { "param_12", 8.0 }, { "param_13", 0.0 }, { "param_14", 0.9 }, { "param_15", 1400.0 },
              { "param_16", 2.5 }, { "param_17", 0.0 }, { "param_18", 0.35 }, { "param_19", 0.0 },
              { "param_20", 0.01 }, { "param_21", 0.4 }, { "param_22", 0.3 }, { "param_23", 0.2 },
              { "param_24", 0.005 }, { "param_25", 0.2 }, { "param_26", 0.85 }, { "param_27", 0.08 },
              { "param_28", 0.4 }, { "param_29", 0.0 }, { "param_30", 1.0 }, { "param_31", 1.0 },
              { "param_32", 0.15 }, { "param_33", 0.0 }, { "param_34", 0.1 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.25 } } }},
        // Short and metallic: hard-sync 2:1 + clocked comb + hard clipping,
        // fast amp decay so it reads as a stab, velocity hard into drive.
        { "Neuro Sync Stab", { "reese_bass", "",
            { { "param_0", 5.0 }, { "param_1", 14.0 }, { "param_2", 0.35 }, { "param_3", 0.0 },
              { "param_4", 0.5 }, { "param_5", 0.2 }, { "param_6", -1.0 }, { "param_7", 0.85 },
              { "param_8", 2.0 }, { "param_9", 0.35 }, { "param_10", 300.0 }, { "param_11", 0.6 },
              { "param_12", 20.0 }, { "param_13", 2.0 }, { "param_14", 0.85 }, { "param_15", 900.0 },
              { "param_16", 4.0 }, { "param_17", 0.0 }, { "param_18", 0.6 }, { "param_19", 0.0 },
              { "param_20", 0.002 }, { "param_21", 0.12 }, { "param_22", 0.1 }, { "param_23", 0.08 },
              { "param_24", 0.002 }, { "param_25", 0.15 }, { "param_26", 0.15 }, { "param_27", 0.06 },
              { "param_28", 0.3 }, { "param_29", 0.0 }, { "param_30", 1.0 }, { "param_31", 1.0 },
              { "param_32", 0.0 }, { "param_33", 0.0 }, { "param_34", 0.0 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.6 } } }},
        // The psy-dub device: half-beat LFO with 0.85 depth on a closed
        // resonant lowpass — the classic wobble while the saws stay static.
        { "Psy Wobble", { "reese_bass", "",
            { { "param_0", 7.0 }, { "param_1", 15.0 }, { "param_2", 0.5 }, { "param_3", 0.0 },
              { "param_4", 0.5 }, { "param_5", 0.3 }, { "param_6", -1.0 }, { "param_7", 0.0 },
              { "param_8", 1.0 }, { "param_9", 0.0 }, { "param_10", 80.0 }, { "param_11", 0.7 },
              { "param_12", 12.0 }, { "param_13", 0.0 }, { "param_14", 0.9 }, { "param_15", 700.0 },
              { "param_16", 6.0 }, { "param_17", 0.0 }, { "param_18", 0.3 }, { "param_19", 0.0 },
              { "param_20", 0.01 }, { "param_21", 0.4 }, { "param_22", 0.5 }, { "param_23", 0.15 },
              { "param_24", 0.005 }, { "param_25", 0.3 }, { "param_26", 0.9 }, { "param_27", 0.1 },
              { "param_28", 0.35 }, { "param_29", 0.0 }, { "param_30", 0.5 }, { "param_31", 1.0 },
              { "param_32", 0.85 }, { "param_33", 0.0 }, { "param_34", 0.2 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.3 } } }},
        // Sub-dominant: 3 voices only, sub at -2 octaves and 0.9 level, low
        // 500 Hz lowpass and almost no drive — pure weight under a kick.
        { "Dub Sub Reese", { "reese_bass", "",
            { { "param_0", 3.0 }, { "param_1", 12.0 }, { "param_2", 0.4 }, { "param_3", 0.0 },
              { "param_4", 0.5 }, { "param_5", 0.9 }, { "param_6", -2.0 }, { "param_7", 0.0 },
              { "param_8", 1.0 }, { "param_9", 0.0 }, { "param_10", 80.0 }, { "param_11", 0.7 },
              { "param_12", 4.0 }, { "param_13", 0.0 }, { "param_14", 0.8 }, { "param_15", 500.0 },
              { "param_16", 1.5 }, { "param_17", 0.0 }, { "param_18", 0.25 }, { "param_19", 0.0 },
              { "param_20", 0.02 }, { "param_21", 0.5 }, { "param_22", 0.4 }, { "param_23", 0.3 },
              { "param_24", 0.01 }, { "param_25", 0.4 }, { "param_26", 0.95 }, { "param_27", 0.25 },
              { "param_28", 0.4 }, { "param_29", 0.0 }, { "param_30", 1.0 }, { "param_31", 1.0 },
              { "param_32", 0.3 }, { "param_33", 0.0 }, { "param_34", 0.0 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.15 } } }},
        // Rolling mid bass: SQUARE shape + a 60% comb + soft-atan drive and a
        // bright 2.6 kHz lowpass — the mid-band "rolling" reese for full-on.
        { "Rolling Mid Reese", { "reese_bass", "",
            { { "param_0", 7.0 }, { "param_1", 18.0 }, { "param_2", 0.5 }, { "param_3", 1.0 },
              { "param_4", 0.5 }, { "param_5", 0.25 }, { "param_6", -1.0 }, { "param_7", 0.0 },
              { "param_8", 1.0 }, { "param_9", 0.6 }, { "param_10", 240.0 }, { "param_11", 0.75 },
              { "param_12", 14.0 }, { "param_13", 1.0 }, { "param_14", 0.85 }, { "param_15", 2600.0 },
              { "param_16", 5.0 }, { "param_17", 0.0 }, { "param_18", 0.5 }, { "param_19", 0.0 },
              { "param_20", 0.002 }, { "param_21", 0.25 }, { "param_22", 0.6 }, { "param_23", 0.08 },
              { "param_24", 0.002 }, { "param_25", 0.2 }, { "param_26", 0.8 }, { "param_27", 0.06 },
              { "param_28", 0.3 }, { "param_29", 1.0 }, { "param_30", 0.25 }, { "param_31", 1.0 },
              { "param_32", 0.4 }, { "param_33", 0.0 }, { "param_34", 0.0 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.4 } } }},
        // The gnarl: 26 dB through the wave-FOLD curve (odd-harmonic edge no
        // other patch has), tri LFO on cutoff, velocity deep into drive.
        { "Fold Gnarl", { "reese_bass", "",
            { { "param_0", 6.0 }, { "param_1", 22.0 }, { "param_2", 0.45 }, { "param_3", 0.0 },
              { "param_4", 0.5 }, { "param_5", 0.3 }, { "param_6", -1.0 }, { "param_7", 0.0 },
              { "param_8", 1.0 }, { "param_9", 0.5 }, { "param_10", 120.0 }, { "param_11", 0.8 },
              { "param_12", 26.0 }, { "param_13", 3.0 }, { "param_14", 0.7 }, { "param_15", 1000.0 },
              { "param_16", 4.0 }, { "param_17", 0.0 }, { "param_18", 0.45 }, { "param_19", 0.0 },
              { "param_20", 0.005 }, { "param_21", 0.3 }, { "param_22", 0.4 }, { "param_23", 0.12 },
              { "param_24", 0.003 }, { "param_25", 0.25 }, { "param_26", 0.7 }, { "param_27", 0.08 },
              { "param_28", 0.3 }, { "param_29", 3.0 }, { "param_30", 0.5 }, { "param_31", 1.0 },
              { "param_32", 0.35 }, { "param_33", 0.0 }, { "param_34", 0.3 }, { "param_35", 0.0 },
              { "param_36", 0.0 }, { "param_37", 1.0 }, { "param_38", 2.0 }, { "param_39", 0.7 } } }},

        // ── growl_bass: the phase-distortion growl voice ─────────────────────
        // MIDI-note fundamental, 1.5:1 sine modulator at half depth, tanh clip,
        // 0.6 env amount on a 900 Hz lowpass — the rolling mid-growl.
        { "Growl Rolling Sub", { "growl_bass", "",
            { { "param_0", 0.0 }, { "param_1", 1.5 }, { "param_2", 0.5 }, { "param_3", 0.0 },
              { "param_4", 0.0 }, { "param_5", 16.0 }, { "param_6", 0.12 }, { "param_7", 8.0 },
              { "param_8", 900.0 }, { "param_9", 5.0 }, { "param_10", 0.6 }, { "param_11", 0.0 },
              { "param_12", 2.0 }, { "param_13", 90.0 }, { "param_14", 0.7 }, { "param_15", 45.0 },
              { "param_16", 0.35 }, { "param_17", 0.0 }, { "param_18", 2.0 }, { "param_19", 12.0 },
              { "param_20", 0.0 }, { "param_21", 0.05 }, { "param_22", 0.0 }, { "param_23", 0.0 },
              { "param_24", 0.0 }, { "param_25", 0.5 } } }},
        // A fixed 55 Hz acid fundamental, square modulator, hard clip and a
        // resonant 650 Hz lowpass with 0.85 env — the growl "303".
        { "Growl Hard Acid", { "growl_bass", "",
            { { "param_0", 55.0 }, { "param_1", 2.0 }, { "param_2", 0.65 }, { "param_3", 2.0 },
              { "param_4", 2.0 }, { "param_5", 30.0 }, { "param_6", 0.25 }, { "param_7", 8.0 },
              { "param_8", 650.0 }, { "param_9", 9.0 }, { "param_10", 0.85 }, { "param_11", 0.0 },
              { "param_12", 1.0 }, { "param_13", 60.0 }, { "param_14", 0.5 }, { "param_15", 30.0 },
              { "param_16", 0.3 }, { "param_17", 0.0 }, { "param_18", 2.0 }, { "param_19", 8.0 },
              { "param_20", 0.0 }, { "param_21", 0.05 }, { "param_22", 0.0 }, { "param_23", 0.0 },
              { "param_24", 0.0 }, { "param_25", 0.5 } } }},
        // Digital grit: triangle modulator, bitcrush clip at 5 bits, 3-voice
        // unison detune 18 cents with deterministic ratio jitter, BANDPASS.
        { "Growl Digital Grit", { "growl_bass", "",
            { { "param_0", 0.0 }, { "param_1", 1.2 }, { "param_2", 0.55 }, { "param_3", 1.0 },
              { "param_4", 3.0 }, { "param_5", 22.0 }, { "param_6", 0.1 }, { "param_7", 5.0 },
              { "param_8", 1200.0 }, { "param_9", 6.0 }, { "param_10", 0.55 }, { "param_11", 1.0 },
              { "param_12", 2.0 }, { "param_13", 120.0 }, { "param_14", 0.6 }, { "param_15", 50.0 },
              { "param_16", 0.3 }, { "param_17", 1.0 }, { "param_18", 3.0 }, { "param_19", 18.0 },
              { "param_20", 1.0 }, { "param_21", 0.15 }, { "param_22", 0.0 }, { "param_23", 0.0 },
              { "param_24", 0.0 }, { "param_25", 0.5 } } }},
        // Vocal formant ON (0.35 morph) + a 2:1 sine modulator at 0.7 depth —
        // the vowel-y growl the Formant switch exists for.
        { "Growl Vocal", { "growl_bass", "",
            { { "param_0", 0.0 }, { "param_1", 2.0 }, { "param_2", 0.7 }, { "param_3", 0.0 },
              { "param_4", 0.0 }, { "param_5", 20.0 }, { "param_6", 0.2 }, { "param_7", 8.0 },
              { "param_8", 1100.0 }, { "param_9", 6.0 }, { "param_10", 0.5 }, { "param_11", 0.0 },
              { "param_12", 2.0 }, { "param_13", 100.0 }, { "param_14", 0.65 }, { "param_15", 45.0 },
              { "param_16", 0.35 }, { "param_17", 0.0 }, { "param_18", 2.0 }, { "param_19", 10.0 },
              { "param_20", 0.0 }, { "param_21", 0.05 }, { "param_22", 1.0 }, { "param_23", 0.35 },
              { "param_24", 0.0 }, { "param_25", 0.5 } } }},

        // ── sub_synth: the clean low-end voice (indices 25..32 left default) ─
        // Sine-only, mono, 400 Hz lowpass, no filter env — the reference pure
        // sub that sits under a kick without a fight for the low-mid band.
        { "Sub Pure", { "sub_synth", "",
            { { "param_0", 0.0 }, { "param_1", 1.0 }, { "param_2", 1.0 }, { "param_3", 0.0 },
              { "param_4", 8.0 }, { "param_5", 0.5 }, { "param_6", -1.0 }, { "param_7", 400.0 },
              { "param_8", 0.05 }, { "param_9", 0.05 }, { "param_10", 0.01 }, { "param_11", 0.3 },
              { "param_12", 0.9 }, { "param_13", 0.2 }, { "param_14", 0.5 }, { "param_15", 0.0 },
              { "param_16", 0.0 }, { "param_17", 0.0 }, { "param_18", 0.0 }, { "param_19", 0.01 },
              { "param_20", 0.3 }, { "param_21", 0.7 }, { "param_22", 0.3 }, { "param_23", 2.0 },
              { "param_24", 0.0 } } }},
        // A 303 read through the sub engine: SQUARE osc, 0.75 resonance, 0.25
        // drive, legato + 0.06 s portamento, 30 units of filter env, no sustain.
        { "Sub Acid 303", { "sub_synth", "",
            { { "param_0", 1.0 }, { "param_1", 0.9 }, { "param_2", 1.0 }, { "param_3", 0.0 },
              { "param_4", 8.0 }, { "param_5", 0.2 }, { "param_6", -1.0 }, { "param_7", 500.0 },
              { "param_8", 0.75 }, { "param_9", 0.25 }, { "param_10", 0.002 }, { "param_11", 0.25 },
              { "param_12", 0.1 }, { "param_13", 0.05 }, { "param_14", 0.5 }, { "param_15", 1.0 },
              { "param_16", 0.06 }, { "param_17", 0.0 }, { "param_18", 30.0 }, { "param_19", 0.001 },
              { "param_20", 0.2 }, { "param_21", 0.0 }, { "param_22", 0.1 }, { "param_23", 2.0 },
              { "param_24", 0.0 } } }},
    };
    return defs;
}

// Hand-built minimal slot JSON for factory files: only fxType/bypassed/name/
// params. presetFromObject is forward-tolerant — plugin/sampler/slicePoints/
// psyFm fields are simply absent.
juce::DynamicObject::Ptr factorySlotObject(const FactorySlotDef& sd)
{
    juce::DynamicObject::Ptr sObj = new juce::DynamicObject();
    sObj->setProperty("fxType", sd.fxType);
    sObj->setProperty("bypassed", false);
    sObj->setProperty("name", sd.name);
    juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
    for (const auto& kv : sd.params)
        paramsObj->setProperty(kv.first, kv.second);
    sObj->setProperty("params", juce::var(paramsObj.get()));
    return sObj;
}

ChainPreset readPresetFile(const juce::File& file, const juce::File& root)
{
    ChainPreset empty;
    if (! file.existsAsFile())
        return empty;
    auto json = juce::JSON::parse(file.loadFileAsString());
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        HDAW_LOG("ChainLibrary",
                 "skipping unparseable preset file: " + file.getFullPathName());
        return empty;
    }
    ChainPreset p = presetFromObject(obj);
    p.id = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    p.isFactory = isFactoryId(p.id);
    return p;
}

} // namespace

ChainLibrary::ChainLibrary(const juce::File& root, Roster roster)
    : root_(root), userDir_(root.getChildFile("user"))
{
    root_.createDirectory();
    root_.getChildFile("_factory").createDirectory();
    userDir_.createDirectory();
    // Seed ONLY the requested roster into _factory/: chains and patches are
    // different content (a chain is several processing slots, a patch is ONE
    // instrument slot's state), so the patch library must not inherit the chain
    // factory chains and vice versa.
    if (roster == Roster::Chains)
        seedFactoryPresetsIfMissing();
    else if (roster == Roster::Patches)
        seedPatchFactoryPresetsIfMissing();
}

void ChainLibrary::seedFactoryPresetsIfMissing()
{
    juce::File factoryDir = root_.getChildFile("_factory");
    for (const auto& def : factoryChainDefs())
    {
        juce::File file = factoryDir.getChildFile(sanitizeFileName(def.name) + ".json");
        // Never overwrite: user edits of a factory file must survive
        // upgrades (seeding is create-if-missing only).
        if (file.existsAsFile())
            continue;
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("version", 1);
        obj->setProperty("name", def.name);
        juce::Array<juce::var> slotsArr;
        for (const auto& sd : def.slots)
            slotsArr.add(juce::var(factorySlotObject(sd).get()));
        obj->setProperty("slots", slotsArr);
        if (! file.replaceWithText(juce::JSON::toString(obj.get(), true)))
            HDAW_LOG("ChainLibrary",
                     "failed to seed factory preset: " + file.getFullPathName());
    }
}

const ChainLibrary& ChainLibrary::userLibrary()
{
    static ChainLibrary lib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("chains"));
    return lib;
}

void ChainLibrary::seedPatchFactoryPresetsIfMissing()
{
    juce::File factoryDir = root_.getChildFile("_factory");
    for (const auto& def : factoryPatchDefs())
    {
        juce::File file = factoryDir.getChildFile(sanitizeFileName(def.name) + ".json");
        // Never overwrite: user edits of a factory patch must survive
        // upgrades (seeding is create-if-missing only, exactly like the chain
        // roster above).
        if (file.existsAsFile())
            continue;
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("version", 1);
        obj->setProperty("name", def.name);
        juce::Array<juce::var> slotsArr;
        // ONE slot: the SAME factorySlotObject() the chain roster builds with,
        // so a patch file's slot serialization cannot drift from a chain's.
        slotsArr.add(juce::var(factorySlotObject(def.slot).get()));
        obj->setProperty("slots", slotsArr);
        if (! file.replaceWithText(juce::JSON::toString(obj.get(), true)))
            HDAW_LOG("ChainLibrary",
                     "failed to seed factory patch: " + file.getFullPathName());
    }
}

const ChainLibrary& ChainLibrary::patchLibrary()
{
    // Sibling of `chains`: one slot's state, not a whole chain. Seeds the
    // built-in bass PATCH roster (Roster::Patches) — NOT the chain factory
    // chains, which are processing presets, a different kind of content.
    static ChainLibrary lib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("patches"),
        Roster::Patches);
    return lib;
}

juce::String ChainLibrary::savePreset(const ChainPreset& p) const
{
    if (p.name.trim().isEmpty())
        return {};

    std::lock_guard<std::mutex> lock(mutex_);

    userDir_.createDirectory();
    juce::String sanitized = sanitizeFileName(p.name);
    juce::File file = userDir_.getChildFile(sanitized + ".json");

    if (file.existsAsFile())
    {
        juce::String baseName = sanitized;
        int counter = 1;
        while (file.existsAsFile() && counter < 100)
        {
            file = userDir_.getChildFile(baseName + "-" + juce::String(counter) + ".json");
            counter++;
        }
        if (file.existsAsFile())
            return {};
    }

    juce::String jsonText = juce::JSON::toString(presetToObject(p).get(), true);
    if (! file.replaceWithText(jsonText))
        return {};

    return file.getRelativePathFrom(root_).replaceCharacter('\\', '/');
}

std::vector<ChainPreset> ChainLibrary::listPresets() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ChainPreset> result;
    auto scanDir = [this, &result](const juce::File& dir, bool factory)
    {
        if (! dir.isDirectory())
            return;
        juce::DirectoryIterator iter(dir, true, "*.json", juce::File::findFiles);
        while (iter.next())
        {
            ChainPreset p = readPresetFile(iter.getFile(), root_);
            if (p.id.isEmpty())
                continue;
            if (p.name.isEmpty())
            {
                HDAW_LOG("ChainLibrary",
                         "skipping preset with empty name: " + iter.getFile().getFullPathName());
                continue;
            }
            p.isFactory = factory;
            result.push_back(std::move(p));
        }
    };
    // Mirror PatternLibrary.cpp:428: factory tree first, then user. The sort
    // makes the order deterministic regardless of DirectoryIterator order:
    // factory group first, then user, each alphabetical by id.
    scanDir(root_.getChildFile("_factory"), true);
    scanDir(userDir_, false);

    std::sort(result.begin(), result.end(),
              [](const ChainPreset& a, const ChainPreset& b)
              {
                  if (a.isFactory != b.isFactory)
                      return a.isFactory;
                  return a.id < b.id;
              });
    return result;
}

ChainPreset ChainLibrary::loadPreset(const juce::String& id) const
{
    ChainPreset empty;
    if (id.isEmpty())
        return empty;

    std::lock_guard<std::mutex> lock(mutex_);

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return empty;

    return readPresetFile(file, root_);
}

bool ChainLibrary::deletePreset(const juce::String& id) const
{
    if (id.isEmpty())
        return false;
    if (isFactoryId(id))
        return false;

    std::lock_guard<std::mutex> lock(mutex_);

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return false;
    if (! file.existsAsFile())
        return false;
    return file.deleteFile();
}

} // namespace HDAW
