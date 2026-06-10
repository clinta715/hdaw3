#pragma once
#include "ClipItem.h"

class MidiClipItem : public ClipItem
{
public:
    MidiClipItem(juce::ValueTree clipTree, double pixelsPerSecond, double bpm = 120.0);
    ~MidiClipItem() override;

protected:
    void paintContent(QPainter& painter, const QRectF& contentRect) override;

private:
    double currentBpm = 120.0;
    int minNote = 36;
    int maxNote = 96;
};
