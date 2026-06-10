#include "MidiClipItem.h"
#include "Theme.h"
#include <QPainter>
#include <juce_audio_basics/juce_audio_basics.h>

MidiClipItem::MidiClipItem(juce::ValueTree tree, double pps, double bpm)
    : ClipItem(tree, pps), currentBpm(bpm)
{
}

MidiClipItem::~MidiClipItem() = default;

void MidiClipItem::paintContent(QPainter& painter, const QRectF& contentRect)
{
    auto midiNotes = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!midiNotes.isValid())
        return;

    int localMinNote = 127;
    int localMaxNote = 0;
    for (int i = 0; i < midiNotes.getNumChildren(); ++i)
    {
        int n = midiNotes.getChild(i).getProperty(IDs::noteNumber);
        localMinNote = std::min(localMinNote, n);
        localMaxNote = std::max(localMaxNote, n);
    }
    if (localMinNote > localMaxNote)
    {
        localMinNote = minNote;
        localMaxNote = maxNote;
    }
    localMinNote = std::max(0, localMinNote - 2);
    localMaxNote = std::min(127, localMaxNote + 2);

    int noteRange = localMaxNote - localMinNote;
    if (noteRange <= 0) return;

    double beatsPerSecond = currentBpm / 60.0;

    double noteHeight = contentRect.height() / static_cast<double>(noteRange);
    if (noteHeight < 2.0) noteHeight = 2.0;

    // Draw piano key labels on left edge if tall enough
    auto noteName = [](int note) -> QString {
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int octave = (note / 12) - 1;
        return QString("%1%2").arg(names[note % 12]).arg(octave);
    };

    if (contentRect.height() > 40)
    {
        painter.setPen(QColor(ThemeColors::textMuted().red(), ThemeColors::textMuted().green(), ThemeColors::textMuted().blue(), 120));
        QFont f = painter.font();
        f.setPointSize(6);
        painter.setFont(f);
        int labelInterval = std::max(1, noteRange / 5);
        for (int n = localMinNote; n <= localMaxNote; n += labelInterval)
        {
            double y = contentRect.bottom() - (n - localMinNote + 0.5) * noteHeight;
            if (y >= contentRect.top() && y <= contentRect.bottom())
                painter.drawText(QPointF(contentRect.x() + 1, y), noteName(n));
        }
    }

    // Draw note bars
    for (int i = 0; i < midiNotes.getNumChildren(); ++i)
    {
        auto note = midiNotes.getChild(i);
        int noteNum = note.getProperty(IDs::noteNumber);
        if (noteNum < localMinNote || noteNum > localMaxNote)
            continue;

        float velocity = note.getProperty(IDs::velocity);
        double startBeat = note.getProperty(IDs::startBeat);
        double durBeats = note.getProperty(IDs::durationBeats);

        double x = startBeat * pixelsPerSecond / beatsPerSecond;
        double nw = std::max(durBeats * pixelsPerSecond / beatsPerSecond, 2.0);
        double y = contentRect.bottom() - (noteNum - localMinNote + 1) * noteHeight;
        double nh = std::max(noteHeight - 1.0, 1.0);

        QRectF nr(contentRect.x() + x, y, nw, nh);

        int alpha = static_cast<int>(velocity * 220.0f + 35.0f);
        QColor noteColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), std::min(255, alpha));

        painter.setPen(QPen(noteColor.darker(130), 0.5));
        painter.setBrush(noteColor);
        painter.drawRoundedRect(nr, 1, 1);
    }
}
