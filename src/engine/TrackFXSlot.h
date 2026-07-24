#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "../common/DebugLog.h"

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
    }

    ~TrackFXSlot();

    juce::String getType() const { return slotType; }
    bool isPlugin() const { return isExternal; }
    bool isBypassed() const { return bypassed.load(std::memory_order_relaxed); }
    void setBypassed(bool b) { bypassed.store(b, std::memory_order_relaxed); }

    const juce::String& getPluginID() const { return pluginIdentifier; }
    bool isIsolated() const { return isolated; }

    juce::AudioPluginInstance* getPluginInstance() const { return pluginInstance.get(); }

    struct ParamInfo {
        juce::String name;
        int index;
        bool automatable = true;
    };

    // Returns the plugin's parameters that report isAutomatable()==true.
    // The cache still indexes by the raw plugin parameter position (so
    // setAutomationParam/applyAutomation use the same indices as
    // pluginInstance->getParameters()); the flag is informational and lets
    // the UI/MCP surface filter out output-only or meter parameters.
    const std::vector<ParamInfo>& getAutomatableParams() const
    {
        return cachedParams;
    }

    void setAutomationParam(int paramIndex, float normalizedValue)
    {
        if (paramIndex >= 0 && paramIndex < numParams.load(std::memory_order_relaxed))
        {
            paramValues[paramIndex].store(normalizedValue, std::memory_order_relaxed);
            paramDirty[paramIndex].store(true, std::memory_order_relaxed);
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
        sampleRate_ = spec.sampleRate;

        if (isExternal && pluginInstance)
        {
            pluginInstance->prepareToPlay(spec.sampleRate, spec.maximumBlockSize);
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
                delay->setDelay(std::max(1, delaySamps));
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
                    *eq->state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        spec.sampleRate, freq, Qval, gDb);
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
            case ActiveType::None:
            default:
                break;
        }
    }

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        if (bypassed.load(std::memory_order_relaxed)) return;

        if (isExternal && pluginInstance)
        {
            pluginInstance->processBlock(buffer, midiMessages);
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
                    int delaySamps = juce::roundToInt(internalParamValues[0] * sampleRate_);
                    delaySamps = std::max(1, delaySamps);
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
        if (reverb)  reverb->reset();
        if (delay)   delay->reset();
        if (eq)      eq->reset();
        if (comp)    comp->reset();
    }

    std::unique_ptr<juce::AudioPluginInstance> releasePlugin()
    {
        return std::move(pluginInstance);
    }

    void showEditor();

    void closeEditor()
    {
        HDAW_LOG("FXSlotCloseEditor", (juce::String("entry this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " editorWindow(before)=" + (editorWindow?"set":"null")).toStdString().c_str());
        // Direct assignment (was: juce::MessageManager::callAsync([this]() { editorWindow = nullptr; })).
        // The async form captured a raw `this`; if the TrackFXSlot was destroyed
        // before the message was delivered, the lambda ran on a dead object
        // (use-after-free write). The close button is invoked on the message
        // thread, so a direct assignment is safe and avoids the lifetime bug.
        editorWindow = nullptr;
    }

    bool isEditorOpen() const { return editorWindow != nullptr; }

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

private:
    enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Plugin };
    ActiveType activeType = ActiveType::None;
    juce::String slotType;
    std::atomic<bool> bypassed{ false };

    bool isExternal = false;
    bool isolated = false;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    std::unique_ptr<juce::DocumentWindow> editorWindow;
    juce::String pluginIdentifier;

    using EQProcessor = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    std::unique_ptr<juce::dsp::Reverb> reverb;
    std::unique_ptr<juce::dsp::DelayLine<float>> delay;
    std::unique_ptr<EQProcessor> eq;
    std::unique_ptr<juce::dsp::Compressor<float>> comp;

    double sampleRate_ = 44100.0;
    std::vector<float> internalParamValues;

    mutable std::vector<ParamInfo> cachedParams;
    std::atomic<int> numParams{ 0 };
    std::unique_ptr<std::atomic<float>[]> paramValues;
    std::unique_ptr<std::atomic<bool>[]> paramDirty;

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
                // Reconstruct all three coeffs from stored values
                *eq->state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    sampleRate_, freq, Qval, gainDb);
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
                }
                break;
            }
            default:
                break;
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
