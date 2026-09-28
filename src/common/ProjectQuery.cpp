#include "ProjectQuery.h"

#include <QJsonArray>

namespace HDAW {

namespace {

QString clipName(const juce::ValueTree& clip)
{
    return QString::fromUtf8(clip.getProperty(IDs::name, "").toString().toRawUTF8());
}

// The clip's [start, start+duration) span in project beats, or an empty span for
// a zero/negative-duration clip (which never sounds). IDs::startTime/IDs::duration
// are SECONDS (McpTools_Read.cpp list_clips).
bool clipBeatSpan(const juce::ValueTree& clip, double bpm, double& startBeat, double& endBeat)
{
    startBeat = querySecondsToBeats(static_cast<double>(clip.getProperty(IDs::startTime)), bpm);
    endBeat = startBeat + querySecondsToBeats(static_cast<double>(clip.getProperty(IDs::duration)), bpm);
    return endBeat > startBeat;
}

bool windowOverlaps(double absStart, double absEnd, double qStart, double qEnd)
{
    return absStart < qEnd && absEnd > qStart;
}

QJsonObject finish(QJsonArray rows)
{
    QJsonObject out;
    out["count"] = rows.size();
    out["unit"] = "beats";   // echoed on purpose: every number is project beats
    out["rows"] = rows;
    return out;
}

} // namespace

QJsonObject buildNoteQueryPayload(const juce::ValueTree& trackList, double bpm,
                                  double startBeat, double endBeat, int trackIndex,
                                  bool* ok, QString* error)
{
    if (ok) *ok = false;
    if (endBeat <= startBeat)
    {
        if (error) *error = QString::fromUtf8(kInvalidBeatWindowError);
        return {};
    }

    QJsonArray rows;
    for (int ti = 0; ti < trackList.getNumChildren(); ++ti)
    {
        if (trackIndex >= 0 && ti != trackIndex) continue;
        auto track = trackList.getChild(ti);
        const int trackID = static_cast<int>(track.getProperty(IDs::trackID, 0));
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
        {
            auto clip = clipList.getChild(ci);
            if (clip.getProperty(IDs::clipType).toString() != juce::String("midi")) continue;
            double clipStartB = 0.0, clipEndB = 0.0;
            if (!clipBeatSpan(clip, bpm, clipStartB, clipEndB)) continue;
            const int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
            const QString name = clipName(clip);
            auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int ni = 0; ni < noteList.getNumChildren(); ++ni)
            {
                auto note = noteList.getChild(ni);
                // IDs::startBeat/durationBeats are CLIP-LOCAL beats; the clip can
                // never sound before it starts and a note tail is TRUNCATED at the
                // clip end (MidiClipProcessor.h:152 drops out past durSec).
                const double rawStart = clipStartB
                    + static_cast<double>(note.getProperty(IDs::startBeat));
                const double rawEnd = rawStart
                    + static_cast<double>(note.getProperty(IDs::durationBeats));
                const double effStart = juce::jmax(rawStart, clipStartB);
                const double effEnd = juce::jmin(rawEnd, clipEndB);
                if (effEnd <= effStart) continue;   // fully clipped away / never sounds
                if (!windowOverlaps(effStart, effEnd, startBeat, endBeat)) continue;

                rows.append(QJsonObject{
                    {"noteId", static_cast<int>(note.getProperty(IDs::noteID, 0))},
                    {"clipId", clipId},
                    {"trackIndex", ti},
                    {"trackID", trackID},
                    {"clipName", name},
                    {"absBeat", effStart},
                    {"endBeat", effEnd},
                    {"localBeat", effStart - clipStartB},
                    {"durationBeats", effEnd - effStart},
                    {"pitch", static_cast<int>(note.getProperty(IDs::noteNumber))},
                    // Same conversion list_notes uses (McpTools_Note.cpp), so the
                    // two note-reading paths cannot disagree on velocity.
                    {"velocity", static_cast<int>(
                        static_cast<double>(note.getProperty(IDs::velocity)) * 127.0 + 0.5)},
                    {"occurrenceIndex", 0},
                    {"truncated", effEnd < rawEnd}
                });
            }
        }
    }

    if (ok) *ok = true;
    return finish(std::move(rows));
}

QJsonObject buildClipQueryPayload(const juce::ValueTree& trackList, double bpm,
                                  double startBeat, double endBeat,
                                  bool* ok, QString* error)
{
    if (ok) *ok = false;
    if (endBeat <= startBeat)
    {
        if (error) *error = QString::fromUtf8(kInvalidBeatWindowError);
        return {};
    }

    QJsonArray rows;
    for (int ti = 0; ti < trackList.getNumChildren(); ++ti)
    {
        auto track = trackList.getChild(ti);
        const int trackID = static_cast<int>(track.getProperty(IDs::trackID, 0));
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
        {
            auto clip = clipList.getChild(ci);
            double clipStartB = 0.0, clipEndB = 0.0;
            if (!clipBeatSpan(clip, bpm, clipStartB, clipEndB)) continue;
            if (!windowOverlaps(clipStartB, clipEndB, startBeat, endBeat)) continue;

            rows.append(QJsonObject{
                {"clipId", static_cast<int>(clip.getProperty(IDs::clipID, 0))},
                {"trackIndex", ti},
                {"trackID", trackID},
                {"name", clipName(clip)},
                {"type", QString::fromUtf8(clip.getProperty(IDs::clipType).toString().toRawUTF8())},
                {"startBeat", clipStartB},
                {"endBeat", clipEndB},
                {"durationBeats", clipEndB - clipStartB},
                {"muted", static_cast<bool>(clip.getProperty(IDs::muted))},
                {"gain", static_cast<double>(clip.getProperty(IDs::gain, 1.0))}
            });
        }
    }

    if (ok) *ok = true;
    return finish(std::move(rows));
}

} // namespace HDAW
