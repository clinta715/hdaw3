#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include "../common/DebugLog.h"
#include "../common/RealtimeGuard.h"
#include "CLAPPluginInstance.h"
#include "../proxy/PluginProxySlot.h"
#include "engine/SamplerEngine.h"
#include "engine/SubtractiveSynthEngine.h"
#include "engine/FmSynthEngine.h"
#include "engine/GrowlBassEngine.h"
#include "engine/SaturatorEngine.h"
#include "DecodedSoundPool.h"
#include "PsyArpEngine.h"
#include "PsyFmEngine.h"
#include "PsyFmAlgorithms.h"
#include "PsyFmState.h"

namespace HDAW {

class TrackFXSlot
{
public:
    struct InternalParamDef {
        int index;
        juce::String name;
        float defaultValue;
        float minValue;
        float maxValue;
    };

    static std::vector<InternalParamDef> getParamDefsForType(const juce::String& type)
    {
        if (type == "reverb")
            return {
                { 0, "Room Size",   0.5f, 0.0f,  1.0f   },
                { 1, "Damping",     0.5f, 0.0f,  1.0f   },
                { 2, "Wet Level",   0.3f, 0.0f,  1.0f   },
                { 3, "Dry Level",   0.7f, 0.0f,  1.0f   },
                { 4, "Width",       1.0f, 0.0f,  1.0f   },
            };
        if (type == "compressor")
            return {
                { 0, "Threshold", -20.0f, -80.0f,  0.0f    },
                { 1, "Ratio",       4.0f,   1.0f, 40.0f    },
                { 2, "Attack",      5.0f,   0.1f,100.0f    },
                { 3, "Release",   100.0f,   1.0f,2000.0f   },
            };
        if (type == "eq")
            return {
                { 0, "Frequency",1000.0f, 20.0f, 20000.0f  },
                { 1, "Q",          0.7f,  0.1f,   10.0f    },
                { 2, "Gain",       0.0f,-24.0f,   24.0f    },
            };
        // Internal delay: Delay Time (manual seconds) + Feedback + Mix, plus
        // tempo-sync controls (P1-3, plan 2026-08-29): SyncToTempo (3) =
        // 1 -> Delay Time is DERIVED from Division (4) + project BPM
        // (seconds = divisionBeatFraction * 60 / bpm, clamped to 0.01..5 s),
        // and re-applied automatically when the tempo changes. Division enum:
        // 0=1/8, 1=1/16, 2=1/32, 3=triplet-1/8 (2/3*1/8), 4=dotted-1/8
        // (1.5*1/8), 5=dotted-1/16 (1.5*1/16), 6=1/4. Beat fractions:
        // 0.125, 0.0625, 0.03125, 0.08333, 0.1875, 0.09375, 0.25.
        if (type == "delay")
            return {
                { 0, "Delay Time", 0.5f, 0.01f,  5.0f    },
                { 1, "Feedback",   0.3f, 0.0f,   0.99f   },
                { 2, "Mix",        0.5f, 0.0f,   1.0f    },
                { 3, "SyncToTempo",0.0f, 0.0f,   1.0f    },
                { 4, "Division",   0.0f, 0.0f,   6.0f    },
            };
        if (type == "chorus")
            return {
                { 0, "Rate",         1.5f,  0.1f,  5.0f   },
                { 1, "Depth",        0.25f, 0.0f,  1.0f   },
                { 2, "Centre Delay", 7.0f,  1.0f, 50.0f   },
                { 3, "Feedback",     0.0f, -1.0f,  1.0f   },
                { 4, "Mix",          0.5f,  0.0f,  1.0f   },
            };
        if (type == "flanger")
            return {
                { 0, "Rate",         0.5f,  0.1f,  5.0f   },
                { 1, "Depth",        0.5f,  0.0f,  1.0f   },
                { 2, "Centre Delay", 3.0f,  1.0f,  5.0f   },
                { 3, "Feedback",     0.5f, -1.0f,  1.0f   },
                { 4, "Mix",          0.5f,  0.0f,  1.0f   },
            };
        if (type == "phaser")
            return {
                { 0, "Rate",             0.5f,    0.1f,    5.0f     },
                { 1, "Depth",            0.5f,    0.0f,    1.0f     },
                { 2, "Centre Frequency", 1000.0f, 20.0f,   20000.0f },
                { 3, "Feedback",         0.0f,   -1.0f,    1.0f     },
                { 4, "Mix",              0.5f,    0.0f,    1.0f     },
            };
        // State-variable filter (lowpass/highpass/bandpass) with an automatable
        // Cutoff — the honest filter sweep for generation scripts (plan
        // 2026-08-29 P1-2). Param 1 is an int enum: 0=lowpass, 1=highpass,
        // 2=bandpass. Automation of Mode is rounded to the nearest enum value.
        if (type == "filter")
            return {
                { 0, "Cutoff",    1000.0f,   20.0f, 20000.0f },
                { 1, "Mode",         0.0f,    0.0f,     2.0f },
                { 2, "Resonance",    0.7f,    0.1f,    10.0f },
            };
        // Drive -> selectable transfer curve -> DC block -> dry/wet mix ->
        // output trim, 2x oversampled (plan 2026-09-02). Type is an int enum
        // (0=SoftTanh, 1=SoftAtan, 2=Hard, 3=Bitcrush); Bits applies to the
        // Bitcrush curve only. Output dB trims the WET path only.
        if (type == "saturator")
            return {
                { 0, "Drive dB",   12.0f,   0.0f,  40.0f },
                { 1, "Type",        0.0f,   0.0f,   3.0f },
                { 2, "Asymmetry",   0.0f,  -1.0f,   1.0f },
                { 3, "Mix",         1.0f,   0.0f,   1.0f },
                { 4, "Output dB",   0.0f, -24.0f,  24.0f },
                { 5, "Bits",        8.0f,   2.0f,  16.0f },
            };
        if (type == "sampler")
            return {
                { 0, "Attack",      0.005f,  0.0f,   5.0f  },
                { 1, "Decay",       0.1f,    0.0f,   5.0f  },
                { 2, "Sustain",     0.9f,    0.0f,   1.0f  },
                { 3, "Release",     0.1f,    0.0f,  10.0f  },
                { 4, "Transpose",   0.0f,  -36.0f,  36.0f  },
                { 5, "SampleStart", 0.0f,    0.0f,   1.0f  },
                { 6, "Hold",        0.0f,    0.0f,   5.0f  },
                { 7, "Glide",       0.0f,    0.0f,   5.0f  },
                { 8, "Reverse",     0.0f,    0.0f,   1.0f  },
                { 9, "SampleEnd",   1.0f,    0.0f,   1.0f  },
            };
        if (type == "fm_synth")
            return {
                { 0, "Algorithm",      0.0f,  0.0f, 31.0f },
                { 1, "Feedback",       5.0f,  0.0f,  7.0f },
                { 2, "Output Level",   0.4f,  0.0f,  1.0f },
                { 3, "OP1 Level",      0.6f,  0.0f,  1.0f },
                { 4, "OP2 Level",      0.6f,  0.0f,  1.0f },
                { 5, "OP3 Level",      0.6f,  0.0f,  1.0f },
                { 6, "OP4 Level",      0.6f,  0.0f,  1.0f },
                { 7, "OP5 Level",      0.6f,  0.0f,  1.0f },
                { 8, "OP6 Level",      0.6f,  0.0f,  1.0f },
                { 9, "OP1 Coarse",     0.0f,  0.0f, 31.0f },
                {10, "OP2 Coarse",     0.0f,  0.0f, 31.0f },
                {11, "OP3 Coarse",     0.0f,  0.0f, 31.0f },
                {12, "OP4 Coarse",     0.0f,  0.0f, 31.0f },
                {13, "OP5 Coarse",     0.0f,  0.0f, 31.0f },
                {14, "OP6 Coarse",     0.0f,  0.0f, 31.0f },
                {15, "OP1 Fine",       0.0f,  0.0f, 99.0f },
                {16, "OP2 Fine",       0.0f,  0.0f, 99.0f },
                {17, "OP3 Fine",       0.0f,  0.0f, 99.0f },
                {18, "OP4 Fine",       0.0f,  0.0f, 99.0f },
                {19, "OP5 Fine",       0.0f,  0.0f, 99.0f },
                {20, "OP6 Fine",       0.0f,  0.0f, 99.0f },
                {21, "LFO Rate",       0.5f,  0.0f,  1.0f },
                {22, "LFO Delay",      0.0f,  0.0f,  1.0f },
                {23, "LFO Pitch Depth",0.0f,  0.0f,  1.0f },
                {24, "LFO Amp Depth",  0.0f,  0.0f,  1.0f },
                {25, "LFO Waveform",   0.0f,  0.0f,  3.0f },
            };
        if (type == "growl_bass")
            return {
                { 0, "Fundamental Hz", 55.0f,  20.0f,  200.0f },
                { 1, "Mod Ratio",       1.5f,   0.5f,    8.0f },
                { 2, "Mod Depth",       0.6f,   0.0f,    1.0f },
                { 3, "Mod Shape",       0.0f,   0.0f,    2.0f },  // 0=Sine, 1=Tri, 2=Square
                { 4, "Clip Type",       0.0f,   0.0f,    3.0f },  // 0=Tanh, 1=Atan, 2=Hard, 3=Bitcrush
                { 5, "Drive dB",       18.0f,   0.0f,   40.0f },
                { 6, "Asymmetry",       0.15f, -1.0f,    1.0f },
                { 7, "Bitcrush Bits",   8.0f,   2.0f,   16.0f },
                { 8, "Filter Cutoff", 800.0f,  20.0f, 20000.0f },
                { 9, "Filter Res",       4.0f,   0.1f,   20.0f },
                {10, "Filter Env Amt",   0.7f,   0.0f,    1.0f },
                {11, "Filter Type",      0.0f,   0.0f,    1.0f },  // 0=LP, 1=BP
                {12, "Attack ms",        2.0f,   0.1f,  100.0f },
                {13, "Decay ms",        80.0f,   1.0f, 1000.0f },
                {14, "Sustain",          0.7f,   0.0f,    1.0f },
                {15, "Release ms",      40.0f,   1.0f, 1000.0f },
                {16, "Output Level",     0.4f,   0.0f,    1.0f },
                {17, "Unison Enable",    0.0f,   0.0f,    1.0f },
                {18, "Unison Voices",    2.0f,   1.0f,    4.0f },
                {19, "Unison Detune",   12.0f,   0.0f,   50.0f },
                {20, "Ratio Jitter",     0.0f,   0.0f,    1.0f },
                {21, "Jitter Amount",    0.05f,  0.0f,    0.5f },
                {22, "Formant Enable",   0.0f,   0.0f,    1.0f },
                {23, "Formant Morph",    0.0f,   0.0f,    1.0f },
                {24, "Sidechain Drive",  0.0f,   0.0f,    1.0f },
                {25, "Sidechain Amt",    0.5f,   0.0f,    1.0f },
            };
        if (type == "psyarp")
            return {
                { 0, "Osc Shape",          0.0f,   0.0f,    2.0f },  // 0=Saw, 1=Square, 2=SuperSaw
                { 1, "Unison Voices",      2.0f,   1.0f,    4.0f },
                { 2, "Unison Detune",      8.0f,   0.0f,   50.0f },
                { 3, "Pattern Shape",      1.0f,   0.0f,    2.0f },  // 0=UpDown, 1=Asym332, 2=Random
                { 4, "Octave Range",       3.0f,   1.0f,    4.0f },
                { 5, "Bars Per Motif",     2.0f,   0.5f,   8.0f },
                { 6, "Filter Cutoff",    600.0f,  20.0f, 20000.0f },
                { 7, "Filter Resonance",   7.0f,   0.1f,   20.0f },
                { 8, "Filter Sweep Bars",  4.0f,   0.5f,   16.0f },
                { 9, "Delay Time (beats)", 0.375f, 0.01f,  2.0f },
                {10, "Delay Feedback",     0.55f,  0.0f,   0.95f },
                {11, "Delay Ping-Pong",    1.0f,   0.0f,   1.0f },
                {12, "Delay Wet",          0.4f,   0.0f,   1.0f },
                {13, "Reverb Size",        3.5f,   0.1f,  10.0f },
                {14, "Reverb Wet on Dry",  0.1f,   0.0f,   1.0f },
                {15, "Reverb Wet on Delay",0.6f,   0.0f,   1.0f },
                {16, "Phaser Enable",      1.0f,   0.0f,   1.0f },
                {17, "Phaser Rate",        0.15f,  0.01f,  5.0f },
                {18, "Phaser Depth",       0.3f,   0.0f,   1.0f },
                {19, "Output Level",       0.4f,   0.0f,   1.0f },
                {20, "Step Rate",          0.0f,   0.0f,   2.0f },  // 0=1/16, 1=1/8, 2=1/4
            };
        if (type == "psy_fm")
            return {
                { 0, "OP1 Ratio",       1.0f,   0.1f,  10.0f },
                { 1, "OP2 Ratio",       1.0f,   0.1f,  10.0f },
                { 2, "OP3 Ratio",       1.0f,   0.1f,  10.0f },
                { 3, "OP4 Ratio",       1.0f,   0.1f,  10.0f },
                { 4, "OP5 Ratio",       1.0f,   0.1f,  10.0f },
                { 5, "OP6 Ratio",       1.0f,   0.1f,  10.0f },
                { 6, "Feedback",        0.0f,   0.0f,   1.0f },
                { 7, "OP1 Attack",      0.01f,  0.001f, 2.0f },
                { 8, "OP1 Decay",       0.3f,   0.001f, 5.0f },
                { 9, "OP1 Sustain",     0.7f,   0.0f,   1.0f },
                {10, "OP1 Release",     0.2f,   0.001f, 5.0f },
                {11, "OP2 Attack",      0.01f,  0.001f, 2.0f },
                {12, "OP2 Decay",       0.3f,   0.001f, 5.0f },
                {13, "OP2 Sustain",     0.7f,   0.0f,   1.0f },
                {14, "OP2 Release",     0.2f,   0.001f, 5.0f },
                {15, "OP3 Attack",      0.01f,  0.001f, 2.0f },
                {16, "OP3 Decay",       0.3f,   0.001f, 5.0f },
                {17, "OP3 Sustain",     0.7f,   0.0f,   1.0f },
                {18, "OP3 Release",     0.2f,   0.001f, 5.0f },
                {19, "OP4 Attack",      0.01f,  0.001f, 2.0f },
                {20, "OP4 Decay",       0.3f,   0.001f, 5.0f },
                {21, "OP4 Sustain",     0.7f,   0.0f,   1.0f },
                {22, "OP4 Release",     0.2f,   0.001f, 5.0f },
                {23, "OP5 Attack",      0.01f,  0.001f, 2.0f },
                {24, "OP5 Decay",       0.3f,   0.001f, 5.0f },
                {25, "OP5 Sustain",     0.7f,   0.0f,   1.0f },
                {26, "OP5 Release",     0.2f,   0.001f, 5.0f },
                {27, "OP6 Attack",      0.01f,  0.001f, 2.0f },
                {28, "OP6 Decay",       0.3f,   0.001f, 5.0f },
                {29, "OP6 Sustain",     0.7f,   0.0f,   1.0f },
                {30, "OP6 Release",     0.2f,   0.001f, 5.0f },
                {31, "Output Level",    0.4f,   0.0f,   1.0f },
                {32, "Algorithm Preset",0.0f,   0.0f,   3.0f },
            };
        if (type == "sub_synth")
            return {
                { 0, "Osc1 Wave",      0.0f,   0.0f,    3.0f },
                { 1, "Osc1 Level",     0.6f,   0.0f,    1.0f },
                { 2, "Osc2 Wave",      1.0f,   0.0f,    3.0f },
                { 3, "Osc2 Level",     0.4f,   0.0f,    1.0f },
                { 4, "Osc2 Detune",    8.0f, -1200.0f, 1200.0f },
                { 5, "Sub Level",      0.35f,  0.0f,    1.0f },
                { 6, "Sub Octave",    -1.0f,  -2.0f,    0.0f },
                { 7, "Cutoff",      1800.0f,  20.0f, 20000.0f },
                { 8, "Resonance",      0.15f,  0.0f,    0.99f },
                { 9, "Drive",          0.0f,   0.0f,    1.0f },
                {10, "Attack",         0.01f,  0.001f,  5.0f },
                {11, "Decay",          0.18f,  0.001f,  5.0f },
                {12, "Sustain",        0.65f,  0.0f,    1.0f },
                {13, "Release",        0.18f,  0.001f,  5.0f },
                {14, "Output Level",   0.8f,   0.0f,    1.5f },
                {15, "Legato",         0.0f,   0.0f,    1.0f },
                {16, "Portamento",     0.0f,   0.0f,    5.0f },
                {17, "Filter Type",     0.0f,   0.0f,    3.0f },
                {18, "Filter Env Amount",24.0f,  0.0f,   48.0f },
                {19, "Filter Attack",   0.01f,  0.001f,  5.0f },
                {20, "Filter Decay",    0.30f,  0.001f,  5.0f },
                {21, "Filter Sustain",  0.70f,  0.0f,    1.0f },
                {22, "Filter Release",  0.30f,  0.001f,  5.0f },
                {23, "Pitch Bend Range",2.0f,   0.0f,   12.0f },
            };
        return {};
    }

    TrackFXSlot(const juce::String& type)
        : slotType(type)
    {
        HDAW_LOG("FXSlotCtor", (juce::String("ctor1 this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " type=" + type.toStdString() + " editorWindow(before)=0x" + juce::String::toHexString((juce::pointer_sized_int)editorWindow.get())).toStdString().c_str());
        if (type == "eq")
            activeType = ActiveType::EQ;
        else if (type == "compressor")
            activeType = ActiveType::Compressor;
        else if (type == "reverb")
            activeType = ActiveType::Reverb;
        else if (type == "delay")
            activeType = ActiveType::Delay;
        else if (type == "chorus")
            activeType = ActiveType::Chorus;
        else if (type == "flanger")
            activeType = ActiveType::Flanger;
        else if (type == "phaser")
            activeType = ActiveType::Phaser;
        else if (type == "filter")
            activeType = ActiveType::Filter;
        else if (type == "sampler")
            activeType = ActiveType::Sampler;
        else if (type == "fm_synth")
            activeType = ActiveType::FmSynth;
        else if (type == "growl_bass")
            activeType = ActiveType::GrowlBass;
        else if (type == "psyarp")
            activeType = ActiveType::PsyArp;
        else if (type == "psy_fm")
            activeType = ActiveType::PsyFm;
        else if (type == "sub_synth")
            activeType = ActiveType::SubSynth;
        else if (type == "saturator")
            activeType = ActiveType::Saturator;
        else if (type == "plugin")
            activeType = ActiveType::Plugin;
        else
            activeType = ActiveType::None;
        if (activeType != ActiveType::None && activeType != ActiveType::Plugin)
        {
            auto defs = getParamDefsForType(type);
            internalParamValues.resize(defs.size());
            for (size_t i = 0; i < defs.size(); ++i)
                internalParamValues[i] = defs[i].defaultValue;
        }
    }

    TrackFXSlot(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& pluginID,
                bool isIsolated = false)
        : slotType("plugin"),
          pluginInstance(std::move(plugin)),
          isExternal(true),
          isolated(isIsolated),
          pluginIdentifier(pluginID)
    {
        HDAW_LOG("FXSlotCtor", (juce::String("ctor2 this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " pluginID=" + pluginID.toStdString() + " isolated=" + (isIsolated?"true":"false") + " pluginInstance=" + (pluginInstance?"ok":"null")).toStdString().c_str());
        activeType = ActiveType::Plugin;
        rebuildParamCache();
        if (isolated)
            wireEditorClosedCallback();
    }

    ~TrackFXSlot();

    juce::String getType() const { return slotType; }
    bool isPlugin() const { return isExternal; }

    bool isBypassed() const { return bypassed.load(std::memory_order_relaxed); }
    void setBypassed(bool b) { bypassed.store(b, std::memory_order_relaxed); }

    // Reported latency of the internal DSP for PDC (summed by
    // Track::updateLatency alongside the hosted-plugin latencies). The
    // oversampler is built with useIntegerLatency=true, so its
    // getLatencyInSamples() is already an integer and the down-path applies
    // the matching fractional delay; roundToInt guards the float
    // representation error (e.g. 168.9999f must not truncate to 168).
    // 0 for every other slot kind.
    int getLatencySamples() const
    {
        return (activeType == ActiveType::Saturator && over_ != nullptr)
            ? juce::roundToInt(over_->getLatencyInSamples())
            : 0;
    }

    const juce::String& getPluginID() const { return pluginIdentifier; }
    bool isIsolated() const { return isolated; }

    // Feed the project tempo (beats per minute) from the audio thread each
    // block (Track::processBlock -> TrackFXSlot::process). Atomic store only —
    // lock-free, no allocation, safe on the audio thread. Tempo-synced delay
    // divisions read this when SyncToTempo (param 3) is on.
    void setTempo(double bpm) { tempoBpm.store(static_cast<float>(bpm), std::memory_order_relaxed); }

    juce::AudioPluginInstance* getPluginInstance() const { return pluginInstance.get(); }

    // Returns the isolated plugin's proxy slot id, or -1 if this slot is not an
    // isolated/external-proxy plugin.
    int proxySlotId() const;

    struct ParamInfo {
        juce::String name;
        int index;
        bool automatable = true;
    };

    // Returns the plugin's parameters that report isAutomatable()==true.
    // For internal FX, returns synthetic ParamInfo entries derived from
    // getInternalParamDefs() so the UI/MCP can enumerate them uniformly.
    const std::vector<ParamInfo>& getAutomatableParams() const
    {
        if (!isExternal && activeType != ActiveType::None)
        {
            // Rebuild internal param info cache if empty or if type changed
            if (cachedParams.empty() && !internalParamValues.empty())
            {
                auto defs = getParamDefsForType(slotType);
                cachedParams.reserve(defs.size());
                for (const auto& d : defs)
                    cachedParams.push_back({ d.name, d.index, true });
            }
        }
        return cachedParams;
    }

    // Set an FX parameter by normalized value (0..1).
    // For external plugins: stores in the atomic param cache for
    // applyAutomation() to push to the plugin.
    // For internal FX: denormalizes to the real param range, stores in
    // internalParamValues, and applies to the live DSP immediately.
    void setAutomationParam(int paramIndex, float normalizedValue)
    {
        if (isExternal)
        {
            if (paramIndex >= 0 && paramIndex < numParams.load(std::memory_order_relaxed))
            {
                paramValues[paramIndex].store(normalizedValue, std::memory_order_relaxed);
                paramDirty[paramIndex].store(true, std::memory_order_relaxed);
            }
        }
        else
        {
            // Internal FX: denormalize 0..1 → real range and apply
            if (paramIndex < 0 || paramIndex >= static_cast<int>(internalParamValues.size()))
                return;
            auto defs = getParamDefsForType(slotType);
            if (paramIndex >= static_cast<int>(defs.size()))
                return;
            float realValue = denormalizeParam(normalizedValue, defs[static_cast<size_t>(paramIndex)]);
            // Lesson-23 contract: automation/modulation is an entry point
            // into internalParamValues — clamp after denormalize exactly like
            // setInternalParam, so out-of-range normalized writes (e.g. >1)
            // can't reach recursive DSP unclamped.
            realValue = clampToParamDef(paramIndex, realValue);
            internalParamValues[static_cast<size_t>(paramIndex)] = realValue;
            applyInternalParamToDsp(paramIndex, realValue);
        }
    }

    // Get an FX parameter as a normalized value (0..1).
    // For external plugins: reads from the atomic param cache.
    // For internal FX: reads from internalParamValues and normalizes.
    float getAutomationParam(int paramIndex) const
    {
        if (isExternal)
        {
            if (paramIndex >= 0 && paramIndex < numParams.load(std::memory_order_relaxed))
                return paramValues[paramIndex].load(std::memory_order_relaxed);
            return 0.0f;
        }
        else
        {
            // Internal FX: normalize real value → 0..1
            if (paramIndex < 0 || paramIndex >= static_cast<int>(internalParamValues.size()))
                return 0.0f;
            auto defs = getParamDefsForType(slotType);
            if (paramIndex >= static_cast<int>(defs.size()))
                return 0.0f;
            return normalizeParam(internalParamValues[static_cast<size_t>(paramIndex)],
                                  defs[static_cast<size_t>(paramIndex)]);
        }
    }

    void applyAutomation()
    {
        if (!isExternal || !pluginInstance) return;
        auto& params = pluginInstance->getParameters();
        int n = (std::min)(static_cast<int>(numParams.load(std::memory_order_relaxed)),
                           static_cast<int>(params.size()));
        for (int i = 0; i < n; ++i)
        {
            // Per-param dirty flag: only push the cached value into the plugin
            // when an automation source (lane playback, MCP set_fx_param) has
            // updated it since the last block. This preserves plugin-GUI edits
            // — without it, applyAutomation runs every block and reverts any
            // knob the user moves in the VST editor back to the stale cache.
            if (!paramDirty[i].load(std::memory_order_relaxed))
                continue;
            paramDirty[i].store(false, std::memory_order_relaxed);

            float v = paramValues[i].load(std::memory_order_relaxed);
            if (v >= 0.0f && v <= 1.0f)
                params[i]->setValue(v);
        }
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        // Lesson 13 tripwire: prepare recreates DSP objects under stateLock;
        // it must run message-side. Best-effort — the existing stateLock
        // still protects the DSP objects.
        if (!HDAW::RealtimeGuard::isMessageThread())
            juce::Logger::writeToLog("TrackFXSlot::prepare off message thread");

        sampleRate_ = spec.sampleRate;

        if (isExternal && pluginInstance)
        {
            // Prepare first: for isolated slots this round-trips PREPARE to
            // the child, which is when the proxy refreshes its reported
            // channel layout from the shm header — so the width read below
            // reflects the hosted plugin's real (possibly multi-port) count.
            pluginInstance->prepareToPlay(spec.sampleRate, spec.maximumBlockSize);
            // Multi-channel plugins (e.g. 4-output CLAP instruments) need a
            // workspace wider than the track's stereo bus: process them in
            // their own width, then downmix channels 0-1 into the track.
            int outCh = pluginInstance->getTotalNumOutputChannels();
            if (auto* clap = dynamic_cast<CLAPPluginInstance*>(pluginInstance.get()))
                outCh = clap->getNumOutputChannels();
            else if (auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get()))
                outCh = proxySlot->getReportedNumOutputChannels();
            pluginWorkspaceChannels = (outCh > static_cast<int>(spec.numChannels)) ? outCh : 0;
            if (pluginWorkspaceChannels > 0)
                pluginWorkspace.setSize(pluginWorkspaceChannels, spec.maximumBlockSize);
            if (auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get()))
                proxySlot->setNumChannels(
                    pluginWorkspaceChannels > 0 ? pluginWorkspaceChannels : spec.numChannels);
            rebuildParamCache();
            return;
        }

        // Clamp the whole vector once so every per-type push below inherits
        // in-range values (legacy/hand-edited projects can store out-of-range
        // param_N; recursive feedback DSP runs away to inf/NaN otherwise).
        for (size_t i = 0; i < internalParamValues.size(); ++i)
            internalParamValues[i] = clampToParamDef(static_cast<int>(i), internalParamValues[i]);

        switch (activeType)
        {
            case ActiveType::Reverb:
            {
                reverb = std::make_unique<juce::dsp::Reverb>();
                reverb->prepare(spec);
                {
                    juce::dsp::Reverb::Parameters p;
                    p.roomSize = (internalParamValues.size() > 0) ? internalParamValues[0] : 0.5f;
                    p.damping  = (internalParamValues.size() > 1) ? internalParamValues[1] : 0.5f;
                    p.wetLevel = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.3f;
                    p.dryLevel = (internalParamValues.size() > 3) ? internalParamValues[3] : 0.7f;
                    p.width    = (internalParamValues.size() > 4) ? internalParamValues[4] : 1.0f;
                    reverb->setParameters(p);
                }
                break;
            }
            case ActiveType::Delay:
            {
                delay = std::make_unique<juce::dsp::DelayLine<float>>(static_cast<int>(spec.sampleRate));
                delay->prepare(spec);
                float delaySec = computeDelaySeconds();
                int delaySamps = juce::roundToInt(delaySec * spec.sampleRate);
                delaySamps = std::max(1, delaySamps);
                delay->setDelay(delaySamps);
                lastDelayTime = delaySec;
                lastDelaySamps = delaySamps;
                break;
            }
            case ActiveType::EQ:
            {
                eq = std::make_unique<EQProcessor>();
                eq->prepare(spec);
                {
                    float freq  = (internalParamValues.size() > 0) ? internalParamValues[0] : 1000.0f;
                    float Qval  = (internalParamValues.size() > 1) ? internalParamValues[1] : 0.7f;
                    float gDb   = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.0f;
                    // Param defs express gain in dB (-24..24); makePeakFilter's
                    // 4th arg is a LINEAR factor (0.0 = silence) - converting
                    // dB->linear made the DEFAULT gain (0 dB) silence every
                    // track carrying an EQ (2026-08-27, psytrance v3).
                    *eq->state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        spec.sampleRate, freq, Qval, juce::Decibels::decibelsToGain(gDb));
                }
                break;
            }
            case ActiveType::Compressor:
            {
                comp = std::make_unique<juce::dsp::Compressor<float>>();
                comp->prepare(spec);
                if (internalParamValues.size() > 0) comp->setThreshold(internalParamValues[0]);
                if (internalParamValues.size() > 1) comp->setRatio(internalParamValues[1]);
                if (internalParamValues.size() > 2) comp->setAttack(internalParamValues[2]);
                if (internalParamValues.size() > 3) comp->setRelease(internalParamValues[3]);
                break;
            }
            case ActiveType::Chorus:
            case ActiveType::Flanger:
            {
                chorusDsp = std::make_unique<juce::dsp::Chorus<float>>();
                chorusDsp->prepare(spec);
                if (internalParamValues.size() > 0) chorusDsp->setRate(internalParamValues[0]);
                if (internalParamValues.size() > 1) chorusDsp->setDepth(internalParamValues[1]);
                if (internalParamValues.size() > 2) chorusDsp->setCentreDelay(internalParamValues[2]);
                if (internalParamValues.size() > 3) chorusDsp->setFeedback(internalParamValues[3]);
                if (internalParamValues.size() > 4) chorusDsp->setMix(internalParamValues[4]);
                break;
            }
            case ActiveType::Phaser:
            {
                phaserDsp = std::make_unique<juce::dsp::Phaser<float>>();
                phaserDsp->prepare(spec);
                if (internalParamValues.size() > 0) phaserDsp->setRate(internalParamValues[0]);
                if (internalParamValues.size() > 1) phaserDsp->setDepth(internalParamValues[1]);
                if (internalParamValues.size() > 2) phaserDsp->setCentreFrequency(internalParamValues[2]);
                if (internalParamValues.size() > 3) phaserDsp->setFeedback(internalParamValues[3]);
                if (internalParamValues.size() > 4) phaserDsp->setMix(internalParamValues[4]);
                break;
            }
            case ActiveType::Filter:
            {
                filter = std::make_unique<juce::dsp::StateVariableTPTFilter<float>>();
                filter->prepare(spec);
                applyFilterParamsFromValues();
                break;
            }
            case ActiveType::Saturator:
            {
                // DC-blocker coefficient is sample-rate aware; default 48k
                // only applies before the first prepare. Engine runs in
                // oversampled domain; keep 20 Hz design cutoff.
                sat_[0].setSampleRate(spec.sampleRate * 2.0f);
                sat_[1].setSampleRate(spec.sampleRate * 2.0f);
                sat_[0].reset();
                sat_[1].reset();
                // First oversampling integration in the codebase (plan
                // 2026-09-02 Task 2). Constructor verified against the
                // FetchContent JUCE 8.0.0 source (build/_deps/juce-src),
                // juce_Oversampling.cpp:548-594: (numChannels, factor,
                // filterType, isMaxQuality, useIntegerLatency) where factor
                // is the EXPONENT (2^factor stages) — factor 1 = 2x
                // oversampling. useIntegerLatency=true keeps the reported
                // latency an exact integer (Track::setLatencySamples is int)
                // and the down-path adds the matching fractional delay.
                over_ = std::make_unique<juce::dsp::Oversampling<float>>(
                    2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                    true, true);
                over_->initProcessing(static_cast<size_t>(spec.maximumBlockSize));
                over_->reset();
                // Push current internalParamValues through the same apply
                // path setInternalParam uses (filter precedent).
                applySaturatorParamsFromValues();
                break;
            }
            case ActiveType::Sampler:
            {
                if (!sampler)
                    sampler = std::make_unique<SamplerEngine>();
                sampler->prepare (spec.sampleRate, static_cast<int> (spec.maximumBlockSize));
                // Push initial params from internalParamValues
                SamplerEngine::Params sp;
                sp.env.attack  = (internalParamValues.size() > 0) ? internalParamValues[0] : 0.005f;
                sp.env.decay   = (internalParamValues.size() > 1) ? internalParamValues[1] : 0.1f;
                sp.env.sustain = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.9f;
                sp.env.release = (internalParamValues.size() > 3) ? internalParamValues[3] : 0.1f;
                sp.transpose   = static_cast<int> ((internalParamValues.size() > 4) ? internalParamValues[4] : 0.0f);
                sp.sampleStart = (internalParamValues.size() > 5) ? internalParamValues[5] : 0.0f;
                sp.env.hold    = (internalParamValues.size() > 6) ? internalParamValues[6] : 0.0f;
                sp.glide       = (internalParamValues.size() > 7) ? internalParamValues[7] : 0.0;
                sp.reverse     = (internalParamValues.size() > 8) ? (internalParamValues[8] >= 0.5f) : false;
                sampler->setParams (sp);
                // If a sound was staged before prepare, adopt it now.
                if (stagedSound_)
                {
                    sampler->setSound (stagedSound_);
                    stagedSound_.reset();
                }
                break;
            }
            case ActiveType::FmSynth:
            {
                if (!fmSynth)
                    fmSynth = std::make_unique<FmSynthEngine>();
                fmSynth->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
                // Push initial params from internalParamValues
                if (internalParamValues.size() > 0) fmSynth->setAlgorithm(static_cast<int>(internalParamValues[0]));
                if (internalParamValues.size() > 1) fmSynth->setFeedback(static_cast<int>(internalParamValues[1]));
                if (internalParamValues.size() > 2) fmSynth->setOutputLevel(internalParamValues[2]);
                for (int op = 0; op < 6 && internalParamValues.size() > (size_t)(3 + op); op++)
                    fmSynth->setOpLevel(op, internalParamValues[3 + op]);
                if (internalParamValues.size() > 9) {
                    for (int op = 0; op < 6 && internalParamValues.size() > (size_t)(9 + op); op++)
                        fmSynth->setOpCoarse(op, static_cast<int>(internalParamValues[9 + op]));
                }
                if (internalParamValues.size() > 15) {
                    for (int op = 0; op < 6 && internalParamValues.size() > (size_t)(15 + op); op++)
                        fmSynth->setOpFine(op, static_cast<int>(internalParamValues[15 + op]));
                }
                if (internalParamValues.size() > 21) fmSynth->setLfoRate(internalParamValues[21]);
                if (internalParamValues.size() > 22) fmSynth->setLfoDelay(internalParamValues[22]);
                if (internalParamValues.size() > 23) fmSynth->setLfoPitchDepth(internalParamValues[23]);
                if (internalParamValues.size() > 24) fmSynth->setLfoAmpDepth(internalParamValues[24]);
                if (internalParamValues.size() > 25) fmSynth->setLfoWaveform(static_cast<int>(internalParamValues[25]));
                break;
            }
            case ActiveType::SubSynth:
            {
                if (!subSynth)
                    subSynth = std::make_unique<SubtractiveSynthEngine>();
                subSynth->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
                if (internalParamValues.size() > 0) subSynth->setOsc1Wave(juce::roundToInt(internalParamValues[0]));
                if (internalParamValues.size() > 1) subSynth->setOsc1Level(internalParamValues[1]);
                if (internalParamValues.size() > 2) subSynth->setOsc2Wave(juce::roundToInt(internalParamValues[2]));
                if (internalParamValues.size() > 3) subSynth->setOsc2Level(internalParamValues[3]);
                if (internalParamValues.size() > 4) subSynth->setOsc2DetuneCents(internalParamValues[4]);
                if (internalParamValues.size() > 5) subSynth->setSubLevel(internalParamValues[5]);
                if (internalParamValues.size() > 6) subSynth->setSubOctave(juce::roundToInt(internalParamValues[6]));
                if (internalParamValues.size() > 7) subSynth->setCutoffHz(internalParamValues[7]);
                if (internalParamValues.size() > 8) subSynth->setResonance(internalParamValues[8]);
                if (internalParamValues.size() > 9) subSynth->setDrive(internalParamValues[9]);
                if (internalParamValues.size() > 10) subSynth->setAttackSeconds(internalParamValues[10]);
                if (internalParamValues.size() > 11) subSynth->setDecaySeconds(internalParamValues[11]);
                if (internalParamValues.size() > 12) subSynth->setSustain(internalParamValues[12]);
                if (internalParamValues.size() > 13) subSynth->setReleaseSeconds(internalParamValues[13]);
                if (internalParamValues.size() > 14) subSynth->setOutputLevel(internalParamValues[14]);
                if (internalParamValues.size() > 15) subSynth->setLegato(internalParamValues[15] >= 0.5f);
                if (internalParamValues.size() > 16) subSynth->setPortamentoSeconds(internalParamValues[16]);
                if (internalParamValues.size() > 17) subSynth->setFilterType(juce::roundToInt(internalParamValues[17]));
                if (internalParamValues.size() > 18) subSynth->setFilterEnvAmount(internalParamValues[18]);
                if (internalParamValues.size() > 19) subSynth->setFilterAttackSeconds(internalParamValues[19]);
                if (internalParamValues.size() > 20) subSynth->setFilterDecaySeconds(internalParamValues[20]);
                if (internalParamValues.size() > 21) subSynth->setFilterSustain(internalParamValues[21]);
                if (internalParamValues.size() > 22) subSynth->setFilterReleaseSeconds(internalParamValues[22]);
                if (internalParamValues.size() > 23) subSynth->setPitchBendRange(internalParamValues[23]);
                break;
            }
            case ActiveType::GrowlBass:
            {
                if (!growlBass)
                    growlBass = std::make_unique<GrowlBassEngine>();
                growlBass->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
                break;
            }
            case ActiveType::PsyArp:
            {
                if (!psyArp)
                    psyArp = std::make_unique<PsyArpEngine>();
                psyArp->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
                // Push initial params from internalParamValues
                if (internalParamValues.size() > 0) psyArp->setOscShape(static_cast<int>(internalParamValues[0]));
                if (internalParamValues.size() > 1) psyArp->setOscUnisonVoices(static_cast<int>(internalParamValues[1]));
                if (internalParamValues.size() > 2) psyArp->setOscDetuneCents(internalParamValues[2]);
                if (internalParamValues.size() > 3) psyArp->setPatternShape(static_cast<int>(internalParamValues[3]));
                if (internalParamValues.size() > 4) psyArp->setOctaveRange(static_cast<int>(internalParamValues[4]));
                if (internalParamValues.size() > 5) psyArp->setBarsPerMotifLoop(internalParamValues[5]);
                if (internalParamValues.size() > 6) psyArp->setFilterCutoffHz(internalParamValues[6]);
                if (internalParamValues.size() > 7) psyArp->setFilterResonance(internalParamValues[7]);
                if (internalParamValues.size() > 8) psyArp->setFilterSweepBars(internalParamValues[8]);
                if (internalParamValues.size() > 9) psyArp->setDelayTimeBeats(internalParamValues[9]);
                if (internalParamValues.size() > 10) psyArp->setDelayFeedback(internalParamValues[10]);
                if (internalParamValues.size() > 11) psyArp->setDelayPingPongWidth(internalParamValues[11]);
                if (internalParamValues.size() > 12) psyArp->setDelayWetLevel(internalParamValues[12]);
                if (internalParamValues.size() > 13) psyArp->setReverbSizeSec(internalParamValues[13]);
                if (internalParamValues.size() > 14) psyArp->setReverbWetOnDry(internalParamValues[14]);
                if (internalParamValues.size() > 15) psyArp->setReverbWetOnDelay(internalParamValues[15]);
                if (internalParamValues.size() > 16) psyArp->setPhaserEnabled(internalParamValues[16] >= 0.5f);
                if (internalParamValues.size() > 17) psyArp->setPhaserRateHz(internalParamValues[17]);
                if (internalParamValues.size() > 18) psyArp->setPhaserDepth(internalParamValues[18]);
                if (internalParamValues.size() > 19) psyArp->setOutputLevel(internalParamValues[19]);
                if (internalParamValues.size() > 20) psyArp->setStepRateIndex(static_cast<int>(internalParamValues[20]));
                break;
            }
            case ActiveType::PsyFm:
            {
                if (!psyFm)
                    psyFm = std::make_unique<PsyFmEngine>();
                psyFm->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
                // Push base ratios from params 0-5
                float ratios[6];
                for (int i = 0; i < 6; i++)
                    ratios[i] = (internalParamValues.size() > (size_t)i) ? internalParamValues[i] : 1.0f;
                psyFm->setBaseRatios(ratios);
                // Feedback
                if (internalParamValues.size() > 6)
                    psyFm->setBaseFeedback(internalParamValues[6]);
                // Per-operator envelopes (params 7-30: 4 params per op)
                for (int op = 0; op < 6; op++)
                {
                    juce::ADSR::Parameters p;
                    p.attack  = (internalParamValues.size() > (size_t)(7 + op * 4))     ? internalParamValues[7 + op * 4]     : 0.01f;
                    p.decay   = (internalParamValues.size() > (size_t)(8 + op * 4))     ? internalParamValues[8 + op * 4]     : 0.3f;
                    p.sustain = (internalParamValues.size() > (size_t)(9 + op * 4))     ? internalParamValues[9 + op * 4]     : 0.7f;
                    p.release = (internalParamValues.size() > (size_t)(10 + op * 4))    ? internalParamValues[10 + op * 4]    : 0.2f;
                    psyFm->setOpEnvelope(op, p);
                }
                // Output level
                if (internalParamValues.size() > 31)
                    psyFm->setOutputLevel(internalParamValues[31]);
                // Algorithm preset
                if (internalParamValues.size() > 32)
                {
                    int algoIdx = static_cast<int>(internalParamValues[32]);
                    switch (algoIdx)
                    {
                        case 0: psyFm->setAlgorithm(growlBassAlgorithm); break;
                        case 1: psyFm->setAlgorithm(acidLeadAlgorithm); break;
                        case 2: psyFm->setAlgorithm(metallicPluckAlgorithm); break;
                        case 3: psyFm->setAlgorithm(riserAlgorithm); break;
                        default: psyFm->setAlgorithm(growlBassAlgorithm); break;
                    }
                }
                else
                {
                    psyFm->setAlgorithm(growlBassAlgorithm);
                }
                break;
            }
            case ActiveType::None:
            default:
                break;
        }
    }

    void forwardPlayHead(juce::AudioPlayHead* ph)
    {
        // TrackFXSlot is not an AudioProcessorGraph node, so the graph's
        // per-block setPlayHead (NodeOp::process) never reaches the hosted
        // plugin instance. Track::processBlock forwards its own playhead
        // here so both in-process CLAP instances and isolated PluginProxySlot
        // children can feed the plugin transport clock. Pointer assignment
        // only — safe on the audio thread (mirrors the graph's own pattern).
        if (isExternal && pluginInstance)
            pluginInstance->setPlayHead(ph);
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        if (bypassed.load(std::memory_order_relaxed)) return;

        if (isExternal && pluginInstance)
        {
            if (pluginWorkspaceChannels > 0)
            {
                pluginWorkspace.clear();
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    pluginWorkspace.copyFrom(ch, 0, buffer, ch, 0, buffer.getNumSamples());
                pluginInstance->processBlock(pluginWorkspace, midiMessages);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.copyFrom(ch, 0, pluginWorkspace, ch, 0, buffer.getNumSamples());
                return;
            }
            pluginInstance->processBlock(buffer, midiMessages);
            return;
        }

        if (activeType == ActiveType::Sampler)
        {
            if (sampler)
            {
                const bool hasRange = (keyRangeLow_ >= 0 && keyRangeHigh_ >= 0
                                       && keyRangeLow_ <= keyRangeHigh_);
                if (hasRange)
                {
                    // Partial sampler: partition MIDI into in-range and remainder.
                    juce::MidiBuffer inRange, remainder;
                    for (const auto metadata : midiMessages)
                    {
                        auto msg = metadata.getMessage();
                        const int note = msg.getNoteNumber();
                        if (note >= keyRangeLow_ && note <= keyRangeHigh_)
                            inRange.addEvent(msg, static_cast<int>(metadata.samplePosition));
                        else
                            remainder.addEvent(msg, static_cast<int>(metadata.samplePosition));
                    }
                    buffer.clear();
                    sampler->render(buffer, inRange);
                    midiMessages = remainder;
                }
                else
                {
                    // Full-range: current behavior (clears + consumes all notes)
                    buffer.clear();
                    sampler->render(buffer, midiMessages);
                    midiMessages.clear();
                }
            }
            return;
        }

        if (activeType == ActiveType::FmSynth)
        {
            if (fmSynth)
            {
                buffer.clear();
                fmSynth->render(buffer, midiMessages);
                midiMessages.clear();
            }
            return;
        }

        if (activeType == ActiveType::GrowlBass)
        {
            if (growlBass)
            {
                buffer.clear();
                growlBass->render(buffer, midiMessages);
                midiMessages.clear();
            }
            return;
        }

        if (activeType == ActiveType::PsyArp)
        {
            if (psyArp)
            {
                buffer.clear();
                psyArp->render(buffer, midiMessages);
                midiMessages.clear();
            }
            return;
        }

        if (activeType == ActiveType::PsyFm)
        {
            if (psyFm)
            {
                buffer.clear();
                psyFm->render(buffer, midiMessages);
                midiMessages.clear();
            }
            return;
        }

        if (activeType == ActiveType::SubSynth)
        {
            if (subSynth)
            {
                subSynth->render(buffer, midiMessages);
                midiMessages.clear();
            }
            return;
        }

        if (activeType == ActiveType::None) return;

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        switch (activeType)
        {
            case ActiveType::Reverb:      if (reverb) reverb->process(context);  break;
            case ActiveType::Delay:
            {
                if (delay && internalParamValues.size() >= 3)
                {
                    float fb = internalParamValues[1];
                    float wetMix = internalParamValues[2];
                    float dryMix = 1.0f - wetMix;
                    // Derived in sync mode (Division * 60 / project BPM,
                    // recomputed every block so tempo changes re-apply
                    // automatically); raw Delay Time param otherwise.
                    float delayTime = computeDelaySeconds();
                    // Only recompute delay samples when the derived delay
                    // changed (param, division, tempo, or manual seconds).
                    if (std::fabs(delayTime - lastDelayTime) > 1e-4f)
                    {
                        lastDelayTime = delayTime;
                        lastDelaySamps = juce::roundToInt(delayTime * sampleRate_);
                        lastDelaySamps = std::max(1, lastDelaySamps);
                    }
                    int delaySamps = lastDelaySamps;
                    auto& outputBlock = context.getOutputBlock();
                    auto numChannels = outputBlock.getNumChannels();
                    auto numSamples = outputBlock.getNumSamples();
                    for (size_t ch = 0; ch < numChannels; ++ch)
                    {
                        auto* channelData = outputBlock.getChannelPointer(ch);
                        for (size_t s = 0; s < numSamples; ++s)
                        {
                            float in = channelData[s];
                            float delayed = delay->popSample(static_cast<int>(ch), delaySamps);
                            delay->pushSample(static_cast<int>(ch), in + delayed * fb);
                            channelData[s] = in * dryMix + delayed * wetMix;
                        }
                    }
                }
                break;
            }
            case ActiveType::EQ:          if (eq)     eq->process(context);      break;
            case ActiveType::Compressor:  if (comp)   comp->process(context);    break;
            case ActiveType::Filter:      if (filter) filter->process(context);  break;
            case ActiveType::Saturator:
            {
                if (over_ == nullptr) break;
                // Mix == 0 exactly is a bit-identical bypass (plan Gate 1):
                // skip the whole oversampled path and leave the block
                // untouched. (Mixing inside the oversampled domain — as
                // below — would still pass the dry through the halfband
                // filters, adding the oversampler latency.)
                if (internalParamValues.size() > 3 && internalParamValues[3] == 0.0f)
                    break;
                // Float reads straight from internalParamValues in process()
                // follow the delay-case precedent (fb/wetMix) — benign tear
                // tolerance, written only under Track::stateLock.
                const float mix = (internalParamValues.size() > 3)
                    ? internalParamValues[3] : 1.0f;
                // Output trim applies to the WET path ONLY, so Mix=0 stays an
                // exact bypass and dry keeps unity gain at any Output dB.
                const float outGain = juce::Decibels::decibelsToGain(
                    (internalParamValues.size() > 4) ? internalParamValues[4] : 0.0f);
                auto upBlock = over_->processSamplesUp(block);
                const auto numCh = (std::min)(upBlock.getNumChannels(),
                                              static_cast<size_t>(2));
                const auto numSamples = upBlock.getNumSamples();
                for (size_t ch = 0; ch < numCh; ++ch)
                {
                    auto* data = upBlock.getChannelPointer(ch);
                    auto& satEngine = sat_[ch < 2 ? ch : 0];
                    for (size_t i = 0; i < numSamples; ++i)
                    {
                        const float dry = data[i];
                        const float wet = outGain * satEngine.processSampleDCBlocked(dry);
                        data[i] = dry + mix * (wet - dry);
                    }
                }
                over_->processSamplesDown(block);
                break;
            }
            case ActiveType::Chorus:
            case ActiveType::Flanger:     if (chorusDsp) chorusDsp->process(context); break;
            case ActiveType::Phaser:      if (phaserDsp) phaserDsp->process(context); break;
            default: break;
        }
    }

    void reset()
    {
        if (isExternal && pluginInstance)
        {
            pluginInstance->reset();
            return;
        }
        if (reverb)    reverb->reset();
        if (delay)     delay->reset();
        if (eq)        eq->reset();
        if (comp)      comp->reset();
        if (chorusDsp) chorusDsp->reset();
        if (phaserDsp) phaserDsp->reset();
        if (filter)    filter->reset();
        if (fmSynth)   fmSynth->prepare(sampleRate_, 0);
        if (growlBass) growlBass->prepare(sampleRate_, 0);
        if (psyArp)    psyArp->prepare(sampleRate_, 0);
        if (psyFm)     psyFm->prepare(sampleRate_, 0);
        if (subSynth)  subSynth->prepare(sampleRate_, 0);
        sat_[0].reset();
        sat_[1].reset();
        if (over_)     over_->reset();
        // Sampler voices stop on sound swap; no explicit reset needed.
    }

    std::unique_ptr<juce::AudioPluginInstance> releasePlugin()
    {
        return std::move(pluginInstance);
    }

    void showEditor();
    void closeEditor();

    bool isEditorOpen() const { return isolated ? remoteEditorOpen.load() : (editorWindow != nullptr); }

    std::vector<InternalParamDef> getInternalParamDefs() const
    {
        if (isExternal || activeType == ActiveType::None)
            return {};
        return getParamDefsForType(slotType);
    }

    std::vector<float> getInternalParamValues() const
    {
        return internalParamValues;
    }

    void setInternalParam(int paramIndex, float value)
    {
        if (paramIndex < 0 || paramIndex >= static_cast<int>(internalParamValues.size()))
            return;
        value = clampToParamDef(paramIndex, value);
        internalParamValues[static_cast<size_t>(paramIndex)] = value;
        applyInternalParamToDsp(paramIndex, value);
    }

    void loadParamsFromTree(const juce::ValueTree& slotTree)
    {
        auto defs = getInternalParamDefs();
        for (const auto& def : defs)
        {
            juce::String propName = "param_" + juce::String(def.index);
            if (slotTree.hasProperty(juce::Identifier(propName)))
            {
                float val = static_cast<float>(slotTree.getProperty(juce::Identifier(propName)));
                val = clampToParamDef(def.index, val);
                internalParamValues[static_cast<size_t>(def.index)] = val;
                applyInternalParamToDsp(def.index, val);
            }
        }
    }

    void loadSamplerState (const juce::ValueTree& slotTree,
                           juce::AudioFormatManager* formatManager = nullptr,
                           HDAW::DecodedSoundPool* decodedPool = nullptr)
    {
        if (activeType != ActiveType::Sampler)
            return;

        // Restore multi-sampler key range from tree (before the sampleFile
        // early-return so ranges survive rebuilds even without a sample).
        keyRangeLow_ = static_cast<int> (slotTree.getProperty ("keyRangeLow", -1));
        keyRangeHigh_ = static_cast<int> (slotTree.getProperty ("keyRangeHigh", -1));

        juce::String sampleFile = slotTree.getProperty ("sampleFile", "").toString();
        if (sampleFile.isEmpty())
            return;

        std::shared_ptr<const HDAW::DecodedSound> decoded;
        if (decodedPool != nullptr)
            decoded = decodedPool->acquire (sampleFile);

        SamplerSound::Builder builder;
        builder.sampleStart = static_cast<double> (slotTree.getProperty ("sampleStart", 0.0));
        builder.sampleEnd   = static_cast<double> (slotTree.getProperty ("sampleEnd", 1.0));
        builder.loopStart   = static_cast<double> (slotTree.getProperty ("loopStart", 0.0));
        builder.loopEnd     = static_cast<double> (slotTree.getProperty ("loopEnd", 1.0));
        builder.loopEnabled = static_cast<bool> (slotTree.getProperty ("loopEnabled", false));
        builder.rootNote = static_cast<int> (slotTree.getProperty ("rootNote", 60));

        if (decoded != nullptr)
        {
            // Pooled path: one decode shared with clips. Copy into the
            // immutable SamplerSound (the pool still owns the canonical data;
            // decode-count stays 1 across rebuilds).
            builder.numChannels = decoded->numChannels;
            builder.length = decoded->length;
            builder.nativeSampleRate = decoded->sampleRate;
            for (int ch = 0; ch < builder.numChannels; ++ch)
            {
                builder.data[ch] = std::make_unique<float[]> (static_cast<size_t> (builder.length));
                std::memcpy (builder.data[ch].get(), decoded->data[ch].get(),
                             static_cast<size_t> (builder.length) * sizeof (float));
            }
        }
        else
        {
            // Fallback: existing direct decode (no pool / local format manager).
            juce::AudioFormatManager localFmt;
            if (! formatManager)
            {
                localFmt.registerBasicFormats();
                formatManager = &localFmt;
            }
            auto* reader = formatManager->createReaderFor (juce::File (sampleFile));
            if (! reader)
                return;
            const int64_t totalSamples = reader->lengthInSamples;
            const int numChannels = static_cast<int> (reader->numChannels);
            builder.numChannels = std::min (numChannels, 2);
            builder.length = totalSamples;
            builder.nativeSampleRate = reader->sampleRate;
            for (int ch = 0; ch < builder.numChannels; ++ch)
                builder.data[ch] = std::make_unique<float[]> (static_cast<size_t> (totalSamples));
            juce::AudioBuffer<float> readBuf (builder.numChannels, static_cast<int> (totalSamples));
            reader->read (&readBuf, 0, static_cast<int> (totalSamples), 0, true, true);
            for (int ch = 0; ch < builder.numChannels; ++ch)
                std::memcpy (builder.data[ch].get(), readBuf.getReadPointer (ch),
                             static_cast<size_t> (totalSamples) * sizeof (float));
            delete reader;
        }

        // Restore slice boundaries from a comma-separated normalized (0..1) string.
        juce::String sliceStr = slotTree.getProperty ("slicePoints", "").toString();
        if (sliceStr.isNotEmpty())
        {
            const int64_t len = builder.length;
            auto tokens = juce::StringArray::fromTokens (sliceStr, ",", "");
            for (auto& tok : tokens)
            {
                tok = tok.trim();
                const double norm = tok.getDoubleValue();
                if (norm < 0.0 || norm > 1.0)
                    continue;
                int64_t frame = static_cast<int64_t> (std::round (norm * static_cast<double> (len)));
                frame = std::clamp<int64_t> (frame, 0, len);
                builder.slicePoints.push_back (frame);
            }
            if (! builder.slicePoints.empty())
            {
                builder.slicePoints.front() = 0;
                builder.slicePoints.back()  = len;
            }
        }

        auto sound = builder.build();

        // Update engine mode from tree
        SamplerEngine::Params sp;
        juce::String mode = slotTree.getProperty ("mode", "classic").toString();
        if (mode == "one-shot" || mode == "OneShot")
            sp.mode = SamplerVoice::Mode::OneShot;
        else if (mode == "slice" || mode == "Slice")
            sp.mode = SamplerVoice::Mode::Slice;
        else
            sp.mode = SamplerVoice::Mode::Classic;
        sp.reverse = static_cast<bool> (slotTree.getProperty ("playReverse", false));
        sp.mono    = static_cast<bool> (slotTree.getProperty ("mono", false));
        sp.transpose = static_cast<int> (slotTree.getProperty ("transpose", 0));
        sp.baseNote  = static_cast<int> (slotTree.getProperty ("baseNote", 60));
        sp.env.attack  = (internalParamValues.size() > 0) ? internalParamValues[0] : 0.005f;
        sp.env.decay   = (internalParamValues.size() > 1) ? internalParamValues[1] : 0.1f;
        sp.env.sustain = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.9f;
        sp.env.release = (internalParamValues.size() > 3) ? internalParamValues[3] : 0.1f;
        sp.env.hold    = (internalParamValues.size() > 6) ? internalParamValues[6] : 0.0f;
        sp.glide       = (internalParamValues.size() > 7) ? internalParamValues[7] : 0.0;
        sp.reverse     = (internalParamValues.size() > 8) ? (internalParamValues[8] >= 0.5f) : false;

        if (sampler)
        {
            sampler->setParams (sp);
            sampler->setSound (sound);
        }
        else
        {
            // Engine not yet prepared — stage the sound for prepare() to adopt.
            stagedSound_ = sound;
            stagedParams_ = sp;
        }
    }

    int getNumPrograms() const
    {
        if (isExternal && pluginInstance)
            return pluginInstance->getNumPrograms();
        return 1;
    }

    int getCurrentProgram() const
    {
        if (isExternal && pluginInstance)
            return pluginInstance->getCurrentProgram();
        return 0;
    }

    juce::String getProgramName(int index) const
    {
        if (isExternal && pluginInstance)
            return pluginInstance->getProgramName(index);
        return {};
    }

    void setCurrentProgram(int index)
    {
        if (isExternal && pluginInstance)
            pluginInstance->setCurrentProgram(index);
    }

    // --- Test hooks ---
    void setSamplerSoundForTest (std::shared_ptr<const SamplerSound> sound)
    {
        if (sampler)
            sampler->setSound (std::move (sound));
        else
            stagedSound_ = std::move (sound);
    }

    const HDAW::SamplerSound* getSamplerSoundForTest() const
    {
        // Raw pointer, never a shared_ptr copy (same race avoidance as
        // SamplerEngine::getSoundForTest: a copy on the message thread would
        // tear against applyPendingSwap's non-atomic swap on the audio thread).
        // The engine only exists after prepare(); tests must prepare() the
        // slot before asserting (the staged-sound path is not exposed here).
        if (sampler == nullptr)
            return nullptr;
        return sampler->getSoundForTest();
    }

    SamplerEngine* samplerEngineForTest() { return sampler.get(); }

    FmSynthEngine* fmSynthEngine() { return fmSynth.get(); }
    PsyFmEngine* psyFmEngine() { return psyFm.get(); }

    // ── PsyFm matrix/sweep state (tree-persisted, rebuild-safe) ──
    // Called on the message thread under Track::stateLock (mirrors
    // setInternalParam's guard): decodes the encoded route string and swaps
    // the live engine's matrix (PsyFmEngine::setModMatrix is itself
    // audio-thread safe). No-op when this slot is not a psy_fm synth.
    void applyModMatrixFromString (const juce::String& encoded)
    {
        if (activeType != ActiveType::PsyFm || ! psyFm) return;
        HDAW::PsyFmModMatrix m;
        for (const auto& r : HDAW::PsyFmState::decodeRoutes (encoded.toStdString()))
            m.addRoute (r);
        psyFm->setModMatrix (std::move (m));
    }

    void applySweepRate (float hz)
    {
        if (activeType != ActiveType::PsyFm || ! psyFm) return;
        // Plain float store — same tolerance as the Track::processBlock FM
        // modulation pass writing the pool from the audio thread.
        psyFm->getModSourcePool().ratioSweepLFORateHz = hz;
    }

    /// Rebuild path (Gate 1/10): restore matrix + sweep rate from the slot
    /// tree. Called after prepare() + loadParamsFromTree() in
    /// Track::rebuildFXChain. Absent properties = defaults (empty matrix,
    /// pool's own default sweep rate).
    void loadPsyFmStateFromTree (const juce::ValueTree& slotTree)
    {
        if (activeType != ActiveType::PsyFm || ! psyFm) return;
        juce::String matrixStr = slotTree.getProperty ("psyFmMatrix", "").toString();
        if (matrixStr.isNotEmpty())
            applyModMatrixFromString (matrixStr);
        auto sweep = slotTree.getProperty ("psyFmSweepRate", -1.0);
        if (static_cast<double> (sweep) >= 0.0)
            applySweepRate (static_cast<float> (static_cast<double> (sweep)));
    }
    GrowlBassEngine* growlBassEngine() { return growlBass.get(); }

    // Multi-sampler key-range routing: when both are >= 0, only MIDI notes
    // in [keyRangeLow_, keyRangeHigh_] are rendered by this sampler; notes
    // outside the range pass to the next slot. -1 = full range (default).
    bool hasKeyRange() const
    {
        return keyRangeLow_ >= 0 && keyRangeHigh_ >= 0
            && keyRangeLow_ <= keyRangeHigh_;
    }

private:
    enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Chorus, Flanger, Phaser, Filter, Plugin, Sampler, FmSynth, GrowlBass, PsyArp, PsyFm, SubSynth, Saturator };
    ActiveType activeType = ActiveType::None;
    juce::String slotType;
    std::atomic<bool> bypassed{ false };

    bool isExternal = false;
    bool isolated = false;
    std::atomic<bool> remoteEditorOpen{false};
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    std::unique_ptr<juce::DocumentWindow> editorWindow;
    juce::String pluginIdentifier;

    using EQProcessor = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    std::unique_ptr<juce::dsp::Reverb> reverb;
    std::unique_ptr<juce::dsp::DelayLine<float>> delay;
    std::unique_ptr<EQProcessor> eq;
    std::unique_ptr<juce::dsp::Compressor<float>> comp;
    std::unique_ptr<juce::dsp::Chorus<float>> chorusDsp;
    std::unique_ptr<juce::dsp::Phaser<float>> phaserDsp;
    std::unique_ptr<juce::dsp::StateVariableTPTFilter<float>> filter;
    std::unique_ptr<SamplerEngine> sampler;
    std::unique_ptr<SubtractiveSynthEngine> subSynth;
    std::unique_ptr<FmSynthEngine> fmSynth;
    std::unique_ptr<GrowlBassEngine> growlBass;
    std::unique_ptr<PsyArpEngine> psyArp;
    std::unique_ptr<PsyFmEngine> psyFm;
    // Saturator: two engines so each channel keeps its own DC-blocker state;
    // oversampler preallocated here in prepare (audio thread only touches it).
    SaturatorEngine sat_[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> over_;

    double sampleRate_ = 44100.0;
    std::vector<float> internalParamValues;

    // Delay parameter cache — avoids recomputing delay samples per sample
    float lastDelayTime = -1.0f;
    int lastDelaySamps = 1;
    // Project tempo fed from Track::processBlock each block (audio-thread
    // atomic store; default 120 BPM). Used by tempo-synced delay divisions.
    std::atomic<float> tempoBpm{ 120.0f };

    mutable std::vector<ParamInfo> cachedParams;
    std::atomic<int> numParams{ 0 };
    std::unique_ptr<std::atomic<float>[]> paramValues;
    std::unique_ptr<std::atomic<bool>[]> paramDirty;

    // Wide-plugin workspace: plugins with more output channels than the
    // track's stereo bus (e.g. 4-channel CLAP instruments) are processed in
    // this buffer, then channels 0-1 are mixed back into the track buffer.
    juce::AudioBuffer<float> pluginWorkspace;
    int pluginWorkspaceChannels = 0;

    std::shared_ptr<const SamplerSound> stagedSound_;
    SamplerEngine::Params stagedParams_;

    // Multi-sampler key-range routing: MIDI note range for this sampler slot.
    // -1 = full range (default, current behavior); 0..127 = restricted range.
    int keyRangeLow_ = -1;
    int keyRangeHigh_ = -1;

    void wireEditorClosedCallback();

    // Normalize a real param value to 0..1 for automation/modulation.
    static float normalizeParam(float realValue, const InternalParamDef& def)
    {
        float range = def.maxValue - def.minValue;
        if (range <= 0.0f) return 0.0f;
        return (realValue - def.minValue) / range;
    }

    // Denormalize a 0..1 automation/modulation value to the real param range.
    static float denormalizeParam(float normalizedValue, const InternalParamDef& def)
    {
        return def.minValue + normalizedValue * (def.maxValue - def.minValue);
    }

    // Tempo-synced delay division beat fractions (P1-3), indexed by the
    // Division param (4): 0=1/8, 1=1/16, 2=1/32, 3=triplet-1/8 (2/3*1/8),
    // 4=dotted-1/8 (1.5*1/8), 5=dotted-1/16 (1.5*1/16), 6=1/4.
    static constexpr float kDelayDivisionBeats[7] = {
        0.125f, 0.0625f, 0.03125f, 0.08333f, 0.1875f, 0.09375f, 0.25f
    };

    bool isDelaySyncOn() const
    {
        return activeType == ActiveType::Delay && internalParamValues.size() > 3
            && internalParamValues[3] > 0.5f;
    }

    // Effective delay seconds. Sync mode: kDelayDivisionBeats[division] *
    // 60 / bpm (bpm <= 0 falls back to 120), clamped to the Delay Time param
    // range 0.01..5 s. Sync off: raw Delay Time param (0). Reads the audio
    // thread's atomic tempo; safe to call from both processBlock and the
    // param-apply path (the cached lastDelayTime compare gates setDelay).
    float computeDelaySeconds() const
    {
        if (isDelaySyncOn())
        {
            int division = (internalParamValues.size() > 4)
                ? juce::roundToInt(internalParamValues[4]) : 0;
            division = juce::jlimit(0, 6, division);
            double bpm = static_cast<double>(tempoBpm.load(std::memory_order_relaxed));
            if (bpm <= 0.0) bpm = 120.0;
            const double sec = static_cast<double>(kDelayDivisionBeats[division]) * 60.0 / bpm;
            return static_cast<float>(juce::jlimit(0.01, 5.0, sec));
        }
        return (internalParamValues.size() > 0) ? internalParamValues[0] : 0.5f;
    }

    // Clamps a raw param value to the slot type's documented range. Params
    // without a def definition pass through unchanged. Out-of-range values
    // reach us from hand-edited/legacy project files and unvalidated command
    // writes; feeding them to recursive DSP (reverb comb feedback, delay/
    // chorus/phaser feedback) causes exponential runaway to inf/NaN — the
    // 2026-08-31 "export silent after 0.6s" bug (see
    // docs/handoffs/2026-09-17-export-silence-investigation.md follow-up).
    float clampToParamDef(int paramIndex, float value) const
    {
        auto defs = getParamDefsForType(slotType);
        if (paramIndex < 0 || paramIndex >= static_cast<int>(defs.size()))
            return value;
        const auto& d = defs[static_cast<size_t>(paramIndex)];
        return juce::jlimit(d.minValue, d.maxValue, value);
    }

    void applyInternalParamToDsp(int paramIndex, float value)
    {
        if (isExternal) return;
        switch (activeType)
        {
            case ActiveType::Reverb:
            {
                if (!reverb) return;
                auto p = reverb->getParameters();
                switch (paramIndex)
                {
                    case 0: p.roomSize = value; break;
                    case 1: p.damping = value;  break;
                    case 2: p.wetLevel = value; break;
                    case 3: p.dryLevel = value; break;
                    case 4: p.width = value;    break;
                    default: return;
                }
                reverb->setParameters(p);
                break;
            }
            case ActiveType::Compressor:
            {
                if (!comp) return;
                switch (paramIndex)
                {
                    case 0: comp->setThreshold(value); break;
                    case 1: comp->setRatio(value);     break;
                    case 2: comp->setAttack(value);    break;
                    case 3: comp->setRelease(value);   break;
                    default: return;
                }
                break;
            }
            case ActiveType::EQ:
            {
                if (!eq) return;
                float freq = internalParamValues[0];
                float Qval = internalParamValues[1];
                float gainDb = internalParamValues[2];
                // Reconstruct all three coeffs from stored values (dB -> linear,
                // see prepare() - passing raw dB as the linear factor silenced
                // the EQ at the default 0 dB gain).
                *eq->state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    sampleRate_, freq, Qval, juce::Decibels::decibelsToGain(gainDb));
                break;
            }
            case ActiveType::Delay:
            {
                if (!delay || paramIndex > 4) break;
                // Sync mode derives the delay from Division + BPM, so a direct
                // Delay Time write is ignored while sync is on (it resumes
                // mattering when sync is turned off). Any other delay param
                // change (SyncToTempo, Division) re-derives and re-applies.
                if (paramIndex == 0 && isDelaySyncOn()) break;
                const float delaySec = computeDelaySeconds();
                if (std::fabs(delaySec - lastDelayTime) > 1e-4f)
                {
                    int delaySamps = juce::roundToInt(delaySec * sampleRate_);
                    delaySamps = std::max(1, delaySamps);
                    delay->setDelay(delaySamps);
                    lastDelayTime = delaySec;
                    lastDelaySamps = delaySamps;
                }
                break;
            }
            case ActiveType::Chorus:
            case ActiveType::Flanger:
            {
                if (!chorusDsp) return;
                switch (paramIndex)
                {
                    case 0: chorusDsp->setRate(value);        break;
                    case 1: chorusDsp->setDepth(value);       break;
                    case 2: chorusDsp->setCentreDelay(value); break;
                    case 3: chorusDsp->setFeedback(value);    break;
                    case 4: chorusDsp->setMix(value);         break;
                    default: return;
                }
                break;
            }
            case ActiveType::Phaser:
            {
                if (!phaserDsp) return;
                switch (paramIndex)
                {
                    case 0: phaserDsp->setRate(value);            break;
                    case 1: phaserDsp->setDepth(value);           break;
                    case 2: phaserDsp->setCentreFrequency(value); break;
                    case 3: phaserDsp->setFeedback(value);        break;
                    case 4: phaserDsp->setMix(value);             break;
                    default: return;
                }
                break;
            }
            case ActiveType::Filter:
            {
                if (!filter) return;
                // Mode is an int enum (0=lowpass, 1=highpass, 2=bandpass).
                // Round fractional automation/command values and store the
                // rounded result so reads report what the DSP actually runs.
                if (paramIndex == 1 && value >= 0.0f && value <= 2.0f)
                {
                    const float rounded = static_cast<float>(juce::roundToInt(value));
                    internalParamValues[static_cast<size_t>(paramIndex)] = rounded;
                    value = rounded;
                }
                // Reconfigure all three filter coefficients from the stored
                // values on ANY param change (mirrors the EQ path: the
                // coefficients depend on all params).
                applyFilterParamsFromValues();
                break;
            }
            case ActiveType::Saturator:
            {
                // Engines are plain members (no null window before prepare):
                // re-push the full state so any of drive/type/asym/bits can
                // move through one path. Mix (3) and Output dB (4) are read
                // by process() from internalParamValues directly (delay
                // precedent) — nothing to push here.
                applySaturatorParamsFromValues();
                break;
            }
            case ActiveType::Sampler:
            {
                if (! sampler) return;
                switch (paramIndex)
                {
                    case 0: sampler->setAttack  (value); break;
                    case 1: sampler->setDecay   (value); break;
                    case 2: sampler->setSustain (value); break;
                    case 3: sampler->setRelease (value); break;
                    case 4: sampler->setTranspose (static_cast<int> (value)); break;
                    case 5: /* sampleStart — handled via loadSamplerState */ break;
                    case 6: sampler->setHold    (value); break;
                    case 7: sampler->setGlide   (value); break;
                    case 8: sampler->setReverse (value >= 0.5f); break;
                    case 9: sampler->setSampleEnd (value); break;
                    default: return;
                }
                break;
            }
            case ActiveType::FmSynth:
            {
                if (!fmSynth) return;
                switch (paramIndex)
                {
                    case 0: fmSynth->setAlgorithm(static_cast<int>(value)); break;
                    case 1: fmSynth->setFeedback(static_cast<int>(value)); break;
                    case 2: fmSynth->setOutputLevel(value); break;
                    case 3: case 4: case 5: case 6: case 7: case 8:
                        fmSynth->setOpLevel(paramIndex - 3, value); break;
                    case 9: case 10: case 11: case 12: case 13: case 14:
                        fmSynth->setOpCoarse(paramIndex - 9, static_cast<int>(value)); break;
                    case 15: case 16: case 17: case 18: case 19: case 20:
                        fmSynth->setOpFine(paramIndex - 15, static_cast<int>(value)); break;
                    case 21: fmSynth->setLfoRate(value); break;
                    case 22: fmSynth->setLfoDelay(value); break;
                    case 23: fmSynth->setLfoPitchDepth(value); break;
                    case 24: fmSynth->setLfoAmpDepth(value); break;
                    case 25: fmSynth->setLfoWaveform(static_cast<int>(value)); break;
                    default: return;
                }
                break;
            }
            case ActiveType::GrowlBass:
            {
                if (!growlBass) return;
                switch (paramIndex)
                {
                    case 0: growlBass->setFundamentalHz(value); break;
                    case 1: growlBass->setModRatio(value); break;
                    case 2: growlBass->setModDepth(value); break;
                    case 3: growlBass->setModShape(static_cast<int>(value)); break;
                    case 4: growlBass->setClipType(static_cast<int>(value)); break;
                    case 5: growlBass->setDriveDb(value); break;
                    case 6: growlBass->setAsymmetry(value); break;
                    case 7: growlBass->setBitcrushBits(static_cast<int>(value)); break;
                    case 8: growlBass->setFilterCutoffHz(value); break;
                    case 9: growlBass->setFilterResonance(value); break;
                    case 10: growlBass->setFilterEnvAmount(value); break;
                    case 11: growlBass->setFilterType(static_cast<int>(value)); break;
                    case 12: growlBass->setAttackMs(value); break;
                    case 13: growlBass->setDecayMs(value); break;
                    case 14: growlBass->setSustainLevel(value); break;
                    case 15: growlBass->setReleaseMs(value); break;
                    case 16: growlBass->setOutputLevel(value); break;
                    case 17: growlBass->setUnisonEnabled(value >= 0.5f); break;
                    case 18: growlBass->setUnisonVoices(static_cast<int>(value)); break;
                    case 19: growlBass->setUnisonDetuneCents(value); break;
                    case 20: growlBass->setPerNoteRatioJitter(value >= 0.5f); break;
                    case 21: growlBass->setRatioJitterAmount(value); break;
                    case 22: growlBass->setFormantEnabled(value >= 0.5f); break;
                    case 23: growlBass->setFormantMorph(value); break;
                    case 24: growlBass->setSidechainDrive(value >= 0.5f); break;
                    case 25: growlBass->setSidechainAmount(value); break;
                    default: return;
                }
                break;
            }
            case ActiveType::PsyArp:
            {
                if (!psyArp) return;
                switch (paramIndex)
                {
                    case 0: psyArp->setOscShape(static_cast<int>(value)); break;
                    case 1: psyArp->setOscUnisonVoices(static_cast<int>(value)); break;
                    case 2: psyArp->setOscDetuneCents(value); break;
                    case 3: psyArp->setPatternShape(static_cast<int>(value)); break;
                    case 4: psyArp->setOctaveRange(static_cast<int>(value)); break;
                    case 5: psyArp->setBarsPerMotifLoop(value); break;
                    case 6: psyArp->setFilterCutoffHz(value); break;
                    case 7: psyArp->setFilterResonance(value); break;
                    case 8: psyArp->setFilterSweepBars(value); break;
                    case 9: psyArp->setDelayTimeBeats(value); break;
                    case 10: psyArp->setDelayFeedback(value); break;
                    case 11: psyArp->setDelayPingPongWidth(value); break;
                    case 12: psyArp->setDelayWetLevel(value); break;
                    case 13: psyArp->setReverbSizeSec(value); break;
                    case 14: psyArp->setReverbWetOnDry(value); break;
                    case 15: psyArp->setReverbWetOnDelay(value); break;
                    case 16: psyArp->setPhaserEnabled(value >= 0.5f); break;
                    case 17: psyArp->setPhaserRateHz(value); break;
                    case 18: psyArp->setPhaserDepth(value); break;
                    case 19: psyArp->setOutputLevel(value); break;
                    case 20: psyArp->setStepRateIndex(static_cast<int>(value)); break;
                    default: return;
                }
                break;
            }
            case ActiveType::PsyFm:
            {
                if (!psyFm) return;
                switch (paramIndex)
                {
                    case 0: case 1: case 2: case 3: case 4: case 5:
                    {
                        float ratios[6];
                        for (int i = 0; i < 6; i++)
                            ratios[i] = (internalParamValues.size() > (size_t)i) ? internalParamValues[i] : 1.0f;
                        ratios[paramIndex] = value;
                        psyFm->setBaseRatios(ratios);
                        break;
                    }
                    case 6: psyFm->setBaseFeedback(value); break;
                    case 7: case 11: case 15: case 19: case 23: case 27:
                    {
                        int op = (paramIndex - 7) / 4;
                        float atk = value;
                        float dec = (internalParamValues.size() > (size_t)(paramIndex + 1)) ? internalParamValues[paramIndex + 1] : 0.3f;
                        float sus = (internalParamValues.size() > (size_t)(paramIndex + 2)) ? internalParamValues[paramIndex + 2] : 0.7f;
                        float rel = (internalParamValues.size() > (size_t)(paramIndex + 3)) ? internalParamValues[paramIndex + 3] : 0.2f;
                        psyFm->setOpEnvelope(op, { atk, dec, sus, rel });
                        break;
                    }
                    case 8: case 12: case 16: case 20: case 24: case 28:
                    {
                        int op = (paramIndex - 8) / 4;
                        float atk = (internalParamValues.size() > (size_t)(paramIndex - 1)) ? internalParamValues[paramIndex - 1] : 0.01f;
                        float dec = value;
                        float sus = (internalParamValues.size() > (size_t)(paramIndex + 1)) ? internalParamValues[paramIndex + 1] : 0.7f;
                        float rel = (internalParamValues.size() > (size_t)(paramIndex + 2)) ? internalParamValues[paramIndex + 2] : 0.2f;
                        psyFm->setOpEnvelope(op, { atk, dec, sus, rel });
                        break;
                    }
                    case 9: case 13: case 17: case 21: case 25: case 29:
                    {
                        int op = (paramIndex - 9) / 4;
                        float atk = (internalParamValues.size() > (size_t)(paramIndex - 2)) ? internalParamValues[paramIndex - 2] : 0.01f;
                        float dec = (internalParamValues.size() > (size_t)(paramIndex - 1)) ? internalParamValues[paramIndex - 1] : 0.3f;
                        float sus = value;
                        float rel = (internalParamValues.size() > (size_t)(paramIndex + 1)) ? internalParamValues[paramIndex + 1] : 0.2f;
                        psyFm->setOpEnvelope(op, { atk, dec, sus, rel });
                        break;
                    }
                    case 10: case 14: case 18: case 22: case 26: case 30:
                    {
                        int op = (paramIndex - 10) / 4;
                        float atk = (internalParamValues.size() > (size_t)(paramIndex - 3)) ? internalParamValues[paramIndex - 3] : 0.01f;
                        float dec = (internalParamValues.size() > (size_t)(paramIndex - 2)) ? internalParamValues[paramIndex - 2] : 0.3f;
                        float sus = (internalParamValues.size() > (size_t)(paramIndex - 1)) ? internalParamValues[paramIndex - 1] : 0.7f;
                        float rel = value;
                        psyFm->setOpEnvelope(op, { atk, dec, sus, rel });
                        break;
                    }
                    case 31: psyFm->setOutputLevel(value); break;
                    case 32:
                    {
                        int algoIdx = static_cast<int>(value);
                        switch (algoIdx)
                        {
                            case 0: psyFm->setAlgorithm(growlBassAlgorithm); break;
                            case 1: psyFm->setAlgorithm(acidLeadAlgorithm); break;
                            case 2: psyFm->setAlgorithm(metallicPluckAlgorithm); break;
                            case 3: psyFm->setAlgorithm(riserAlgorithm); break;
                            default: psyFm->setAlgorithm(growlBassAlgorithm); break;
                        }
                        break;
                    }
                    default: return;
                }
                break;
            }
            case ActiveType::SubSynth:
            {
                if (!subSynth) return;
                switch (paramIndex)
                {
                    case 0: subSynth->setOsc1Wave(juce::roundToInt(value)); break;
                    case 1: subSynth->setOsc1Level(value); break;
                    case 2: subSynth->setOsc2Wave(juce::roundToInt(value)); break;
                    case 3: subSynth->setOsc2Level(value); break;
                    case 4: subSynth->setOsc2DetuneCents(value); break;
                    case 5: subSynth->setSubLevel(value); break;
                    case 6: subSynth->setSubOctave(juce::roundToInt(value)); break;
                    case 7: subSynth->setCutoffHz(value); break;
                    case 8: subSynth->setResonance(value); break;
                    case 9: subSynth->setDrive(value); break;
                    case 10: subSynth->setAttackSeconds(value); break;
                    case 11: subSynth->setDecaySeconds(value); break;
                    case 12: subSynth->setSustain(value); break;
                    case 13: subSynth->setReleaseSeconds(value); break;
                    case 14: subSynth->setOutputLevel(value); break;
                    case 15: subSynth->setLegato(value >= 0.5f); break;
                    case 16: subSynth->setPortamentoSeconds(value); break;
                    case 17: subSynth->setFilterType(juce::roundToInt(value)); break;
                    case 18: subSynth->setFilterEnvAmount(value); break;
                    case 19: subSynth->setFilterAttackSeconds(value); break;
                    case 20: subSynth->setFilterDecaySeconds(value); break;
                    case 21: subSynth->setFilterSustain(value); break;
                    case 22: subSynth->setFilterReleaseSeconds(value); break;
                    case 23: subSynth->setPitchBendRange(value); break;
                    default: return;
                }
                break;
            }
            default:
                break;
        }
    }

    // Reconfigure the state-variable filter from stored internalParamValues.
    // Cutoff is clamped just below Nyquist (the TPT filter's cutoff setter
    // asserts frequency < sampleRate/2) and kept >= 1 Hz, so a 20 kHz cutoff
    // never trips the assert at low sample rates.
    void applyFilterParamsFromValues()
    {
        if (!filter) return;
        const float cutoff = (internalParamValues.size() > 0) ? internalParamValues[0] : 1000.0f;
        const float mode   = (internalParamValues.size() > 1) ? internalParamValues[1] : 0.0f;
        const float res    = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.7f;
        const int m = juce::jlimit(0, 2, juce::roundToInt(mode));
        const float sr = static_cast<float>(sampleRate_);
        const float maxCut = std::max(1.0f, sr * 0.49f);
        const float freq = juce::jlimit(1.0f, maxCut, cutoff);
        filter->setType(m == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                      : m == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                               : juce::dsp::StateVariableTPTFilterType::bandpass);
        filter->setCutoffFrequency(freq);
        filter->setResonance(res);
    }

    // Push the stored saturator params into both channel engines. Type (1)
    // and Bits (5) are int enums: round fractional automation/command values
    // and store the rounded result so reads report what the DSP actually
    // runs (filter Mode pattern :1375-1380). Callers hold Track::stateLock
    // (setInternalParam / prepare) per the lesson-13 DSP-write contract.
    void applySaturatorParamsFromValues()
    {
        if (internalParamValues.size() > 1)
        {
            const float rounded = static_cast<float>(juce::roundToInt(internalParamValues[1]));
            internalParamValues[1] = juce::jlimit(0.0f, 3.0f, rounded);
        }
        if (internalParamValues.size() > 5)
        {
            const float rounded = static_cast<float>(juce::roundToInt(internalParamValues[5]));
            internalParamValues[5] = juce::jlimit(2.0f, 16.0f, rounded);
        }
        const float driveDb = (internalParamValues.size() > 0) ? internalParamValues[0] : 12.0f;
        const int   type    = (internalParamValues.size() > 1) ? juce::roundToInt(internalParamValues[1]) : 0;
        const float asym    = (internalParamValues.size() > 2) ? internalParamValues[2] : 0.0f;
        const float bits    = (internalParamValues.size() > 5) ? internalParamValues[5] : 8.0f;
        for (auto& engine : sat_)
        {
            engine.setDriveDb(driveDb);
            engine.setType(type);
            engine.setAsymmetry(asym);
            engine.setBits(bits); // engine rounds/clamps internally
        }
    }

    void rebuildParamCache()
    {
        cachedParams.clear();
        if (!isExternal || !pluginInstance)
        {
            numParams = 0;
            paramValues.reset();
            paramDirty.reset();
            return;
        }
        auto& params = pluginInstance->getParameters();
        int n = params.size();
        cachedParams.reserve(n);
        paramValues = std::make_unique<std::atomic<float>[]>(n);
        paramDirty = std::make_unique<std::atomic<bool>[]>(n);
        numParams = n;
        for (int i = 0; i < n; ++i)
        {
            cachedParams.push_back({params[i]->getName(64), i, params[i]->isAutomatable()});
            paramValues[i].store(params[i]->getValue(), std::memory_order_relaxed);
            paramDirty[i].store(false, std::memory_order_relaxed);
        }
    }
};

} // namespace HDAW
