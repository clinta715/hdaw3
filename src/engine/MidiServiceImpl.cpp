#include "MidiServiceImpl.h"
#include "MidiInputManager.h"

MidiServiceImpl::MidiServiceImpl(HDAW::MidiInputManager& mgr) : mgr_(mgr) {}
MidiServiceImpl::~MidiServiceImpl() = default;

std::vector<std::string> MidiServiceImpl::getAvailableDevices()
{
    std::vector<std::string> result;
    auto devices = mgr_.getAvailableDevices();
    for (const auto& d : devices)
        result.push_back(d.toStdString());
    return result;
}

bool MidiServiceImpl::openDevice(const std::string& identifier)
{
    return mgr_.openDevice(juce::String(identifier));
}

void MidiServiceImpl::closeDevice()
{
    mgr_.closeDevice();
}
