#include "AudioImport.h"
#include "../model/ProjectModel.h"
#include "../engine/ProjectPool.h"
#include "../common/DebugLog.h"
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

bool HDAW::normalizeAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath)
{
    auto& pool = engine.getProjectPool();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        pool.getFormatManager().createReaderFor(juce::File(sourcePath.toUtf8().constData())));
    if (reader == nullptr)
    {
        HDAW_LOG("AudioImport", "normalize: could not read file: " + sourcePath);
        return false;
    }

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);

    // Read all audio
    for (int ch = 0; ch < numChannels; ++ch)
        reader->read(&buffer, ch, 1, 0, numSamples, true);

    // Find peak
    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float chPeak = buffer.findMinMax(ch, 0, numSamples).getEnd();
        if (chPeak > peak) peak = chPeak;
    }

    if (peak < 0.0001f)
    {
        HDAW_LOG("AudioImport", "normalize: audio is essentially silent");
        outPath = sourcePath;
        return false;
    }

    float gain = 1.0f / peak;
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGain(ch, 0, numSamples, gain);

    // Write normalized file
    QFileInfo fi(sourcePath);
    QString newPath = fi.path() + "/" + fi.completeBaseName() + "_normalized." + fi.suffix();
    juce::File outputFile(newPath.toUtf8().constData());
    auto fileStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
    if (fileStream == nullptr)
    {
        HDAW_LOG("AudioImport", "normalize: could not create output stream: " + newPath);
        return false;
    }
    juce::WavAudioFormat wavFormat;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wavFormat.createWriterFor(fileStream.get(), reader->sampleRate,
                                  numChannels, 16, {}, 0));
    if (writer == nullptr)
    {
        HDAW_LOG("AudioImport", "normalize: could not write file: " + newPath);
        return false;
    }
    fileStream.release(); // writer takes ownership

    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);

    outPath = newPath;
    HDAW_LOG("AudioImport", "normalized audio: " + newPath);
    return true;
}

bool HDAW::reverseAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath)
{
    auto& pool = engine.getProjectPool();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        pool.getFormatManager().createReaderFor(juce::File(sourcePath.toUtf8().constData())));
    if (reader == nullptr)
    {
        HDAW_LOG("AudioImport", "reverse: could not read file: " + sourcePath);
        return false;
    }

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        reader->read(&buffer, ch, 1, 0, numSamples, true);

    // Reverse each channel
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.reverse(ch, 0, numSamples);

    // Write reversed file
    QFileInfo fi(sourcePath);
    QString newPath = fi.path() + "/" + fi.completeBaseName() + "_reversed." + fi.suffix();
    juce::File outputFile(newPath.toUtf8().constData());
    auto fileStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
    if (fileStream == nullptr)
    {
        HDAW_LOG("AudioImport", "reverse: could not create output stream: " + newPath);
        return false;
    }
    juce::WavAudioFormat wavFormat;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wavFormat.createWriterFor(fileStream.get(), reader->sampleRate,
                                  numChannels, 16, {}, 0));
    if (writer == nullptr)
    {
        HDAW_LOG("AudioImport", "reverse: could not write file: " + newPath);
        return false;
    }
    fileStream.release(); // writer takes ownership

    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);

    outPath = newPath;
    HDAW_LOG("AudioImport", "reversed audio: " + newPath);
    return true;
}
