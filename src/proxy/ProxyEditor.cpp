#include "ProxyEditor.h"
#include "PluginProxySlot.h"

namespace proxy {

ProxyEditor::ProxyEditor(PluginProxySlot& s)
    : AudioProcessorEditor(s), slot(s)
{
    setSize(300, 120);

    addAndMakeVisible(nameLabel);
    nameLabel.setText(slot.getName(), juce::dontSendNotification);
    nameLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    nameLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(openEditorButton);
    openEditorButton.onClick = [this] { onOpenEditorClicked(); };

    addAndMakeVisible(bypassButton);

    addAndMakeVisible(crashButton);
    crashButton.setColour(juce::TextButton::buttonColourId,
                          juce::Colour::fromRGB(200, 60, 60));
    crashButton.setVisible(slot.isCrashed());
    crashButton.onClick = [this] { onCrashRestart(); };
}

ProxyEditor::~ProxyEditor() = default;

void ProxyEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(30, 30, 30));

    if (slot.isCrashed()) {
        g.setColour(juce::Colour::fromRGB(200, 60, 60));
        g.setFont(14.0f);
        g.drawText("Plugin crashed!", getLocalBounds().removeFromBottom(30),
                   juce::Justification::centred);
    }
}

void ProxyEditor::resized() {
    auto area = getLocalBounds();
    nameLabel.setBounds(area.removeFromTop(40));
    area.removeFromTop(5);
    bypassButton.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);
    openEditorButton.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);
    crashButton.setBounds(area.removeFromTop(25));
}

void ProxyEditor::onOpenEditorClicked() {
    auto* pipe = slot.getProcessManager().getPipe(slot.getSlotId());
    if (!pipe) {
        openEditorButton.setEnabled(false);
        return;
    }

    proxy::ProxyMessage msg{};
    msg.type = proxy::MessageType::SHOW_EDITOR;
    msg.slotId = slot.getSlotId();

    static constexpr DWORD kShowEditorTimeoutMs = 2000;
    if (!pipe->sendMsgBounded(msg, kShowEditorTimeoutMs)) {
        openEditorButton.setEnabled(false);
        return;
    }

    proxy::ProxyResponse resp{};
    if (!pipe->receiveRespBounded(resp, kShowEditorTimeoutMs)) {
        openEditorButton.setEnabled(false);
        return;
    }

    slot.startEditorWatcher();
}

void ProxyEditor::onCrashRestart() {
    slot.restartAfterCrash();
}

} // namespace proxy
