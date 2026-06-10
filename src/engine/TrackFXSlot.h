#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

namespace HDAW {

class TrackFXSlot
{
public:
    TrackFXSlot(const juce::String& type)
        : slotType(type)
    {
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
    }

    TrackFXSlot(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& pluginID)
        : slotType("plugin"),
          pluginInstance(std::move(plugin)),
          isExternal(true),
          pluginIdentifier(pluginID)
    {
        activeType = ActiveType::Plugin;
    }

    ~TrackFXSlot() = default;

    juce::String getType() const { return slotType; }
    bool isPlugin() const { return isExternal; }
    bool isBypassed() const { return bypassed.load(std::memory_order_relaxed); }
    void setBypassed(bool b) { bypassed.store(b, std::memory_order_relaxed); }

    const juce::String& getPluginID() const { return pluginIdentifier; }

    juce::AudioPluginInstance* getPluginInstance() const { return pluginInstance.get(); }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        if (isExternal && pluginInstance)
        {
            pluginInstance->prepareToPlay(spec.sampleRate, spec.maximumBlockSize);
            return;
        }

        switch (activeType)
        {
            case ActiveType::Reverb:
            {
                reverb = std::make_unique<juce::dsp::Reverb>();
                reverb->prepare(spec);
                reverb->setParameters({ 0.5f, 0.5f, 0.3f, 0.7f });
                break;
            }
            case ActiveType::Delay:
            {
                delay = std::make_unique<juce::dsp::DelayLine<float>>(static_cast<int>(spec.sampleRate));
                delay->prepare(spec);
                delay->setDelay(static_cast<int>(spec.sampleRate * 0.5f));
                break;
            }
            case ActiveType::EQ:
            {
                eq = std::make_unique<EQProcessor>();
                eq->prepare(spec);
                *eq->state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    spec.sampleRate, 1000.0f, 0.7f, 0.0f);
                break;
            }
            case ActiveType::Compressor:
            {
                comp = std::make_unique<juce::dsp::Compressor<float>>();
                comp->prepare(spec);
                comp->setThreshold(-20.0f);
                comp->setRatio(4.0f);
                comp->setAttack(5.0f);
                comp->setRelease(100.0f);
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
            case ActiveType::Delay:       if (delay)  delay->process(context);   break;
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

    void showEditor()
    {
        if (editor != nullptr || pluginInstance == nullptr)
            return;

        editor.reset(pluginInstance->createEditor());
        if (editor != nullptr)
            editor->setVisible(true);
    }

    void closeEditor()
    {
        if (editor == nullptr)
            return;

        editor->setVisible(false);
        editor.reset();
    }

    bool isEditorOpen() const { return editor != nullptr; }

private:
    enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Plugin };
    ActiveType activeType = ActiveType::None;
    juce::String slotType;
    std::atomic<bool> bypassed{ false };

    bool isExternal = false;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::String pluginIdentifier;

    using EQProcessor = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    std::unique_ptr<juce::dsp::Reverb> reverb;
    std::unique_ptr<juce::dsp::DelayLine<float>> delay;
    std::unique_ptr<EQProcessor> eq;
    std::unique_ptr<juce::dsp::Compressor<float>> comp;
};

} // namespace HDAW
