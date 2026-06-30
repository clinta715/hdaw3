#include "AudioImport.h"
#include "../model/ProjectModel.h"
#include "../engine/ProjectPool.h"
#include "../ui/DebugLog.h"
#include <QFileInfo>
#include <juce_audio_formats/juce_audio_formats.h>

bool HDAW::importAudioFile(AudioEngine& engine, const QString& path, int trackIdx)
{
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    int resolvedTrack = trackIdx;
    if (resolvedTrack < 0)
    {
        if (trackList.getNumChildren() == 0)
        {
            HDAW_LOG("AudioImport", "no tracks available and no trackIdx supplied");
            return false;
        }
        resolvedTrack = 0;
    }
    if (resolvedTrack >= trackList.getNumChildren())
    {
        HDAW_LOG("AudioImport", "trackIdx out of range: " + QString::number(resolvedTrack));
        return false;
    }

    auto trackTree = trackList.getChild(resolvedTrack);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &model.getUndoManager());
    }

    QFileInfo fi(path);
    double duration = 4.0;
    auto& pool = engine.getProjectPool();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        pool.getFormatManager().createReaderFor(juce::File(path.toUtf8().constData())));
    if (reader != nullptr)
    {
        duration = reader->lengthInSamples / reader->sampleRate;
    }
    else
    {
        HDAW_LOG("AudioImport", "could not create audio reader for: " + path);
    }

    double startTime = 0.0;
    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        auto c = clipList.getChild(i);
        double end = static_cast<double>(c.getProperty(IDs::startTime))
                   + static_cast<double>(c.getProperty(IDs::duration));
        startTime = (std::max)(startTime, end);
    }

    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              startTime, duration,
                                              path.toUtf8().constData());
    clipList.addChild(clip, -1, &model.getUndoManager());

    engine.getMainProcessor()->rebuildRoutingGraph();
    return true;
}
