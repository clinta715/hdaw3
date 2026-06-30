#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <string>

namespace proxy {

class ShmRegion {
public:
    ShmRegion() = default;
    ~ShmRegion();

    ShmRegion(const ShmRegion&) = delete;
    ShmRegion& operator=(const ShmRegion&) = delete;

    bool create(const std::string& name, uint32_t totalSize);
    bool open(const std::string& name);
    void close();

    ShmHeader* getHeader() const;
    float* getInputRing() const;
    float* getOutputRing() const;
    MidiEvent* getMidiInRing() const;
    MidiEvent* getMidiOutRing() const;

    bool writeInput(const float* data, uint32_t count);
    bool readInput(float* data, uint32_t count);
    bool writeOutput(const float* data, uint32_t count);
    bool readOutput(float* data, uint32_t count);

private:
    HANDLE hMap = INVALID_HANDLE_VALUE;
    void* basePtr = nullptr;
    uint32_t totalSize = 0;
};

} // namespace proxy
