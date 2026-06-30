#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include "../ui/DebugLog.h"

namespace HDAW {

class PluginEditorWindow : public juce::DocumentWindow
{
public:
PluginEditorWindow(juce::AudioProcessorEditor* editor,
                   const juce::String& pluginName,
                   std::function<void()> onClose)
    : juce::DocumentWindow(pluginName,
                           juce::Colour::fromRGB(40, 40, 40),
                           juce::DocumentWindow::closeButton),
      onCloseCallback(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(true, false);

    int editorW = editor->getWidth();
    int editorH = editor->getHeight();
    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: editor size=" +
        juce::String(editorW) + "x" + juce::String(editorH));

    if (auto* peer = getPeer())
    {
        juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: peer HWND=" +
            juce::String::toHexString((juce::pointer_sized_int)peer->getNativeHandle()));
    }
    else
    {
        juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: peer=null after setContentOwned!");
    }

    centreWithSize(editorW, editorH);
    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: bounds=" +
        getBounds().toString());

    setVisible(true);
    toFront(true);
    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: setVisible(true) called, isVisible=" +
        juce::String(isVisible() ? "true" : "false"));
}

    void closeButtonPressed() override
    {
        if (onCloseCallback)
            onCloseCallback();
    }

private:
    std::function<void()> onCloseCallback;
};

class TrackFXSlot
{
public:
    TrackFXSlot(const juce::String& type)
        : slotType(type)
    {
        HDAW_LOG("FXSlotCtor", QString::fromStdString((juce::String("ctor1 this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " type=" + type.toStdString() + " editorWindow(before)=0x" + juce::String::toHexString((juce::pointer_sized_int)editorWindow.get())).toStdString()));
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

    TrackFXSlot(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& pluginID,
                bool isIsolated = false)
        : slotType("plugin"),
          pluginInstance(std::move(plugin)),
          isExternal(true),
          isolated(isIsolated),
          pluginIdentifier(pluginID)
    {
        HDAW_LOG("FXSlotCtor", QString::fromStdString((juce::String("ctor2 this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " pluginID=" + pluginID.toStdString() + " isolated=" + (isIsolated?"true":"false") + " pluginInstance=" + (pluginInstance?"ok":"null")).toStdString()));
        activeType = ActiveType::Plugin;
    }

    ~TrackFXSlot();

    juce::String getType() const { return slotType; }
    bool isPlugin() const { return isExternal; }
    bool isBypassed() const { return bypassed.load(std::memory_order_relaxed); }
    void setBypassed(bool b) { bypassed.store(b, std::memory_order_relaxed); }

    const juce::String& getPluginID() const { return pluginIdentifier; }
    bool isIsolated() const { return isolated; }

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
        juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor entry: editorWindow=" +
            juce::String(editorWindow != nullptr ? "exists" : "null") +
            " pluginInstance=" + juce::String(pluginInstance != nullptr ? "ok" : "null") +
            " isPlugin=" + juce::String(isPlugin() ? "true" : "false"));

        if (editorWindow != nullptr || pluginInstance == nullptr)
            return;

        auto* ed = pluginInstance->createEditor();
        juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor: createEditor=" +
            juce::String(ed != nullptr ? "ok" : "null"));
        if (ed == nullptr)
            return;

        auto onClose = [this]() { closeEditor(); };
        editorWindow = std::make_unique<PluginEditorWindow>(
            ed, pluginInstance->getName(), std::move(onClose));
        juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor: editorWindow created, isEditorOpen=" +
            juce::String(isEditorOpen() ? "true" : "false"));
    }

    void closeEditor()
    {
        HDAW_LOG("FXSlotCloseEditor", QString::fromStdString((juce::String("entry this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " editorWindow(before)=" + (editorWindow?"set":"null")).toStdString()));
        // Direct assignment (was: juce::MessageManager::callAsync([this]() { editorWindow = nullptr; })).
        // The async form captured a raw `this`; if the TrackFXSlot was destroyed
        // before the message was delivered, the lambda ran on a dead object
        // (use-after-free write). The close button is invoked on the message
        // thread, so a direct assignment is safe and avoids the lifetime bug.
        editorWindow = nullptr;
    }

    bool isEditorOpen() const { return editorWindow != nullptr; }

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
};

} // namespace HDAW
