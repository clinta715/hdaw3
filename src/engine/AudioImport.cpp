#include "AudioImport.h"
#include "../model/ProjectModel.h"
#include "../engine/ProjectPool.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <juce_audio_formats/juce_audio_formats.h>

bool HDAW::importAudioFile(AudioEngine& engine, QWidget* parent, const QString& path)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackList.getNumChildren() == 0) return false;

    QStringList trackNames;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto name = QString::fromUtf8(
            trackList.getChild(i).getProperty(IDs::name).toString().toRawUTF8());
        trackNames << QString("Track %1: %2").arg(i + 1).arg(name);
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(parent, "Select Track",
        "Import to which track?", trackNames, 0, false, &ok);
    if (!ok || selected.isEmpty()) return false;

    int trackIndex = trackNames.indexOf(selected);
    if (trackIndex < 0) return false;

    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &engine.getProjectModel().getUndoManager());
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
    clipList.addChild(clip, -1, &engine.getProjectModel().getUndoManager());

    engine.getMainProcessor()->rebuildRoutingGraph();
    return true;
}
