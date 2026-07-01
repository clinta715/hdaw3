#include "MidiInputManager.h"

namespace HDAW {

MidiInputManager::~MidiInputManager()
{
    closeDevice();
}

std::vector<juce::String> MidiInputManager::getAvailableDevices()
{
    std::vector<juce::String> devices;
    for (const auto& devInfo : juce::MidiInput::getAvailableDevices())
        devices.push_back(devInfo.identifier);
    return devices;
}

bool MidiInputManager::openDevice(const juce::String& identifier)
{
    closeDevice();

    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& devInfo : devices)
    {
        if (devInfo.identifier == identifier)
        {
            currentInput = juce::MidiInput::openDevice(devInfo.identifier, this);
            if (currentInput != nullptr)
            {
                currentInput->start();
                return true;
            }
        }
    }
    return false;
}

void MidiInputManager::closeDevice()
{
    if (currentInput != nullptr)
    {
        currentInput->stop();
        currentInput = nullptr;
    }
}

juce::String MidiInputManager::getOpenDeviceName() const
{
    if (currentInput != nullptr)
        return currentInput->getName();
    return {};
}

void MidiInputManager::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    if (noteCallback)
        noteCallback(message);
}

} // namespace HDAW
