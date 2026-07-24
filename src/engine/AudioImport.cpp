#include "AudioImport.h"
#include "AudioEngine.h"
#include "BpmDetector.h"
#include "BarSnap.h"
#include "../model/ProjectModel.h"
#include "../engine/ProjectPool.h"
#include "../common/DebugLog.h"
#include <QFileInfo>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

double HDAW::readBpmFromMetadata(juce::AudioFormatReader* reader)
{
    if (reader == nullptr) return 0.0;
    auto& metadata = reader->metadataValues;
    auto keys = metadata.getAllKeys();
    for (int i = 0; i < keys.size(); ++i)
    {
        auto key = keys[i];
        if (key.toLowerCase().contains("bpm"))
        {
            auto val = metadata.getValue(keys[i], {}).trim();
            double bpm = val.getDoubleValue();
            if (bpm > 0.0 && bpm < 1000.0)
                return bpm;
        }
    }
    return 0.0;
}

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

    double clipOffset = 0.0;
    double clipDuration = duration;
    if (reader != nullptr)
    {
        auto bounds = detectSilenceBounds(*reader);
        clipOffset = bounds.leadingSeconds;
        clipDuration = duration - bounds.leadingSeconds - bounds.trailingSeconds;
        if (clipDuration < 0.01) { clipOffset = 0.0; clipDuration = duration; }
    }

    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              startTime, clipDuration,
                                              path.toUtf8().constData());
    if (clipOffset > 0.0)
        clip.setProperty(IDs::offset, clipOffset, &model.getUndoManager());
    clipList.addChild(clip, -1, &model.getUndoManager());

    if (reader != nullptr)
    {
        double bpm = readBpmFromMetadata(reader.get());

        // Fallback: aubio onset detection if no metadata BPM
        if (bpm <= 0.0 && reader->numChannels > 0)
        {
            const int maxSamples = static_cast<int>(30.0 * reader->sampleRate);
            const int totalSamples = static_cast<int>(reader->lengthInSamples);
            const int n = (std::min)(totalSamples, maxSamples);
            std::vector<float> buf(n);
            reader->read(&buf, 0, n, 0, true, false);
            auto det = BpmDetector::detect(buf.data(), n, reader->sampleRate);
            bpm = det.bpm;
        }

        if (bpm > 0.0)
        {
            clip.setProperty(IDs::sourceBpm, bpm, &model.getUndoManager());
        }
    }

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

HDAW::SilenceBounds HDAW::detectSilenceBounds(juce::AudioFormatReader& reader, float threshold)
{
    SilenceBounds result;
    const int64_t totalSamples = static_cast<int64_t>(reader.lengthInSamples);
    if (totalSamples <= 0) return result;

    const double sr = reader.sampleRate;
    const int numChannels = static_cast<int>(reader.numChannels);
    constexpr int blockSize = 4096;

    juce::AudioBuffer<float> tempBuffer(numChannels, blockSize);

    int64_t firstNonSilent = totalSamples;
    for (int64_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(blockSize), totalSamples - pos));
        for (int ch = 0; ch < numChannels; ++ch)
            reader.read(&tempBuffer, ch, 1, pos, toRead, true);

        for (int i = 0; i < toRead; ++i)
        {
            bool above = false;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (std::abs(tempBuffer.getSample(ch, i)) >= threshold)
                {
                    above = true;
                    break;
                }
            }
            if (above)
            {
                firstNonSilent = pos + i;
                break;
            }
        }
        if (firstNonSilent < totalSamples) break;
    }

    if (firstNonSilent >= totalSamples)
        return result;

    int64_t lastNonSilent = 0;
    for (int64_t pos = totalSamples - blockSize; pos >= 0; pos -= blockSize)
    {
        int64_t readStart = (std::max)(static_cast<int64_t>(0), pos);
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(blockSize), totalSamples - readStart));
        for (int ch = 0; ch < numChannels; ++ch)
            reader.read(&tempBuffer, ch, 1, readStart, toRead, true);

        for (int i = toRead - 1; i >= 0; --i)
        {
            bool above = false;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (std::abs(tempBuffer.getSample(ch, i)) >= threshold)
                {
                    above = true;
                    break;
                }
            }
            if (above)
            {
                lastNonSilent = readStart + i;
                break;
            }
        }
        if (lastNonSilent > 0) break;
    }

    result.leadingSeconds = static_cast<double>(firstNonSilent) / sr;
    result.trailingSeconds = static_cast<double>(totalSamples - 1 - lastNonSilent) / sr;

    double audible = static_cast<double>(totalSamples) / sr - result.leadingSeconds - result.trailingSeconds;
    if (audible < 0.01)
    {
        result.leadingSeconds = 0.0;
        result.trailingSeconds = 0.0;
    }

    return result;
}
