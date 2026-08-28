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
#include "engine/FmSynthEngine.h"
#include "DecodedSoundPool.h"

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
        if (type == "delay")
            return {
                { 0, "Delay Time", 0.5f, 0.01f,  5.0f    },
                { 1, "Feedback",   0.3f, 0.0f,   0.99f   },
                { 2, "Mix",        0.5f, 0.0f,   1.0f    },
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

    const juce::String& getPluginID() const { return pluginIdentifier; }
    bool isIsolated() const { return isolated; }

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
            pluginWorkspaceChannels = (outCh > spec.numChannels) ? outCh : 0;
            if (pluginWorkspaceChannels > 0)
                pluginWorkspace.setSize(pluginWorkspaceChannels, spec.maximumBlockSize);
            if (auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get()))
                proxySlot->setNumChannels(
                    pluginWorkspaceChannels > 0 ? pluginWorkspaceChannels : spec.numChannels);
            rebuildParamCache();
            return;
        }

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
                float delaySec = (internalParamValues.size() > 0) ? internalParamValues[0] : 0.5f;
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
                buffer.clear();    // instrument = source (overwrite)
                sampler->render (buffer, midiMessages);
                midiMessages.clear();
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
                    float delayTime = internalParamValues[0];
                    // Only recompute delay samples when the parameter changes
                    if (delayTime != lastDelayTime)
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

private:
    enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Chorus, Flanger, Phaser, Filter, Plugin, Sampler, FmSynth };
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
    std::unique_ptr<FmSynthEngine> fmSynth;

    double sampleRate_ = 44100.0;
    std::vector<float> internalParamValues;

    // Delay parameter cache — avoids recomputing delay samples per sample
    float lastDelayTime = -1.0f;
    int lastDelaySamps = 1;

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
                if (!delay) return;
                if (paramIndex == 0)
                {
                    int delaySamps = juce::roundToInt(value * sampleRate_);
                    delaySamps = std::max(1, delaySamps);
                    delay->setDelay(delaySamps);
                    lastDelayTime = value;
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
