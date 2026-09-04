#include "TrackFXSlot.h"
#include "../proxy/PluginProxySlot.h"

// SubSynth wiring stays in the header-defined TrackFXSlot implementation.

int HDAW::TrackFXSlot::proxySlotId() const
{
    auto* p = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get());
    return p ? static_cast<int>(p->getSlotId()) : -1;
}

HDAW::TrackFXSlot::~TrackFXSlot()
{
    HDAW_LOG("FXSlotDtor",
        (juce::String("this=") + juce::String::toHexString((juce::pointer_sized_int)this) +
          " editorWindow(before delete)=" + (editorWindow ? "set" : "null") +
          " rawPtr=0x" + juce::String::toHexString((juce::pointer_sized_int)editorWindow.get()) +
         " pluginInstance=" + (pluginInstance ? "ok" : "null") +
         " slotType=" + slotType.toStdString()).toStdString().c_str());
}

void HDAW::TrackFXSlot::wireEditorClosedCallback()
{
    auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get());
    if (proxySlot) {
        proxySlot->setEditorClosedCallback([this]() {
            remoteEditorOpen.store(false);
        });
    }
}

void HDAW::TrackFXSlot::showEditor()
{
    if (pluginInstance == nullptr)
        return;

    if (isolated)
    {
        if (remoteEditorOpen.load())
            return;
        auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get());
        if (!proxySlot)
            return;
        auto* pipe = proxySlot->getProcessManager().getPipe(proxySlot->getSlotId());
        if (!pipe)
            return;

        proxy::ProxyMessage msg{};
        msg.type = proxy::MessageType::SHOW_EDITOR;
        msg.slotId = proxySlot->getSlotId();
        pipe->sendMsg(msg);

        proxy::ProxyResponse resp{};
        pipe->receiveResp(resp);
        remoteEditorOpen.store(true);
        return;
    }

    if (editorWindow != nullptr)
        return;

    auto* ed = pluginInstance->createEditor();
    if (ed == nullptr)
        return;

    editorWindow = std::make_unique<juce::DocumentWindow>(
        pluginInstance->getName(),
        juce::Colours::black,
        juce::DocumentWindow::closeButton);
    editorWindow->setContentOwned(ed, true);
    editorWindow->centreWithSize(ed->getWidth(), ed->getHeight());
    editorWindow->setVisible(true);
}

void HDAW::TrackFXSlot::closeEditor()
{
    if (isolated)
    {
        if (!remoteEditorOpen.load())
            return;
        auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get());
        if (proxySlot)
        {
            auto* pipe = proxySlot->getProcessManager().getPipe(proxySlot->getSlotId());
            if (pipe)
            {
                proxy::ProxyMessage msg{};
                msg.type = proxy::MessageType::CLOSE_EDITOR;
                msg.slotId = proxySlot->getSlotId();
                pipe->sendMsg(msg);

                proxy::ProxyResponse resp{};
                pipe->receiveResp(resp);
            }
        }
        remoteEditorOpen.store(false);
        return;
    }

    HDAW_LOG("FXSlotCloseEditor", (juce::String("entry this=") + juce::String::toHexString((juce::pointer_sized_int)this) + " editorWindow(before)=" + (editorWindow?"set":"null")).toStdString().c_str());
    editorWindow = nullptr;
}
