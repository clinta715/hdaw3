#pragma once
#include <juce_core/juce_core.h>
#include <vector>

struct ParamUpdate {
    int trackIndex;
    int paramID;
    float value;
    int clipIndex = -1;
};

class SPSCBridge
{
public:
    SPSCBridge(int capacity = 1024)
        : fifo(capacity), buffer(capacity)
    {
    }

    bool pushUpdate(const ParamUpdate& update)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 + size2 < 1)
            return false;

        if (size1 > 0)
            buffer[start1] = update;
        else
            buffer[start2] = update;

        fifo.finishedWrite(1);
        return true;
    }

    template <typename Callback>
    void popUpdates(Callback&& callback)
    {
        int start1, size1, start2, size2;
        int numReady = fifo.getNumReady();
        
        if (numReady <= 0)
            return;

        fifo.prepareToRead(numReady, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            callback(buffer[start1 + i]);

        for (int i = 0; i < size2; ++i)
            callback(buffer[start2 + i]);

        fifo.finishedRead(size1 + size2);
    }

private:
    juce::AbstractFifo fifo;
    std::vector<ParamUpdate> buffer;
};
