#pragma once
#include <string>
#include <vector>

class MidiService {
public:
    virtual ~MidiService() = default;

    virtual std::vector<std::string> getAvailableDevices() = 0;
    virtual bool openDevice(const std::string& identifier) = 0;
    virtual void closeDevice() = 0;
};
