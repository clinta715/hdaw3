#pragma once
#include "../common/MidiService.h"

namespace HDAW { class MidiInputManager; }

class MidiServiceImpl : public MidiService {
public:
    explicit MidiServiceImpl(HDAW::MidiInputManager& mgr);
    ~MidiServiceImpl() override;

    std::vector<std::string> getAvailableDevices() override;
    bool openDevice(const std::string& identifier) override;
    void closeDevice() override;

private:
    HDAW::MidiInputManager& mgr_;
};
