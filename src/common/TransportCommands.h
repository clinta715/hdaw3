#pragma once
#include <cstdint>

class TransportCommands
{
public:
    virtual ~TransportCommands() = default;

    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void rewind() = 0;
    virtual void toggleLoop() = 0;

    virtual void seekToSample(int64_t sample) = 0;
    virtual void seekToSeconds(double seconds) = 0;

    virtual void startRecording() = 0;
    virtual void stopRecording() = 0;
    virtual bool isRecording() const = 0;
};
