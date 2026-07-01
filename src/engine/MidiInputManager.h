#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <vector>
#include <functional>

namespace HDAW {

class MidiInputManager : public juce::MidiInputCallback
{
public:
    using NoteCallback = std::function<void(const juce::MidiMessage&)>;

    MidiInputManager() = default;
    ~MidiInputManager() override;

    std::vector<juce::String> getAvailableDevices();
    bool openDevice(const juce::String& identifier);
    void closeDevice();
    bool isDeviceOpen() const { return currentInput != nullptr; }
    juce::String getOpenDeviceName() const;

    void setNoteCallback(NoteCallback cb) { noteCallback = std::move(cb); }
    void setThruEnabled(bool enabled) { thruEnabled = enabled; }
    bool isThruEnabled() const { return thruEnabled; }

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    std::unique_ptr<juce::MidiInput> currentInput;
    NoteCallback noteCallback;
    bool thruEnabled = false;
};

} // namespace HDAW
