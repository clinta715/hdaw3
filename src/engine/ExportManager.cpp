#include "ExportManager.h"

namespace HDAW {

ExportManager::ExportManager() = default;

ExportManager::~ExportManager()
{
    cancel();
    if (renderThread.joinable())
        renderThread.join();
}

bool ExportManager::startExport(const juce::ValueTree& projectTree,
                                juce::AudioFormatManager& formatManager,
                                PluginManager* pluginManager, const juce::File& outputPath,
                                double sampleRate, double startTime, double duration,
                                Format format, int bitDepth)
{
    if (active.load())
        return false;

    cancelFlag = false;
    active = true;

    if (renderThread.joinable())
        renderThread.join();

    juce::ValueTree treeCopy = projectTree.createCopy();

    renderThread = std::thread(&ExportManager::renderThreadFunc, this,
                               treeCopy, &formatManager, pluginManager,
                               outputPath, sampleRate, startTime, duration,
                               format, bitDepth);

    return true;
}

void ExportManager::cancel()
{
    cancelFlag = true;
}

double ExportManager::calculateProjectDuration(ProjectModel& model)
{
    double maxEnd = 0.0;
    auto trackList = model.getTrackListTree();

    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;

        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double start = clip.getProperty(IDs::startTime);
            double dur = clip.getProperty(IDs::duration);
            maxEnd = std::max(maxEnd, start + dur);
        }
    }

    return std::max(maxEnd + 3.0, 4.0); // at least 4 seconds, add 3s tail
}

void ExportManager::renderThreadFunc(juce::ValueTree treeCopy,
                                     juce::AudioFormatManager* formatManager,
                                     PluginManager* pluginManager, juce::File outputPath,
                                     double sampleRate, double startTime, double duration,
                                     Format format, int bitDepth)
{
    ProjectModel localModel;
    localModel.getTree().copyPropertiesFrom(treeCopy, nullptr);
    localModel.getTree().removeAllChildren(nullptr);
    for (int i = 0; i < treeCopy.getNumChildren(); ++i)
        localModel.getTree().addChild(treeCopy.getChild(i).createCopy(), -1, nullptr);

    TransportManager renderTransport;
    renderTransport.setSampleRate(sampleRate);
    renderTransport.setBPM(localModel.getTree().getProperty(IDs::tempo, 120.0));
    renderTransport.setPlaying(true);
    renderTransport.setCurrentSample(static_cast<int64_t>(startTime * sampleRate));

    InternalPlayHead renderPlayHead(renderTransport);

    juce::AudioProcessorGraph renderGraph;
    renderGraph.setPlayHead(&renderPlayHead);

    RoutingManager routingManager(renderGraph, localModel, *formatManager,
                                  renderTransport, pluginManager);
    routingManager.rebuildFromValueTree();

    const int blockSize = 512;
    renderGraph.prepareToPlay(sampleRate, blockSize);

    int64_t totalSamples = static_cast<int64_t>(duration * sampleRate);
    int64_t totalBlocks = (totalSamples + blockSize - 1) / blockSize;
    int64_t blocksDone = 0;

    // Select format
    std::unique_ptr<juce::AudioFormat> audioFormat;
    switch (format)
    {
        case WAV:  audioFormat = std::make_unique<juce::WavAudioFormat>();  break;
        case AIFF: audioFormat = std::make_unique<juce::AiffAudioFormat>(); break;
        case FLAC: audioFormat = std::make_unique<juce::FlacAudioFormat>(); break;
    }

    auto* outStream = outputPath.createOutputStream().release();
    bool success = false;
    juce::String message;

    if (outStream == nullptr)
    {
        message = "Could not create output file.";
        goto finish;
    }

    {
        auto* writer = audioFormat->createWriterFor(outStream, sampleRate, 2, bitDepth, {}, 0);
        if (writer == nullptr)
        {
            delete outStream;
            message = "Could not create audio writer.";
            goto finish;
        }

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midiBuffer;
        int64_t samplesRendered = 0;

        while (samplesRendered < totalSamples && !cancelFlag.load())
        {
            int numThisBlock = static_cast<int>(std::min(static_cast<int64_t>(blockSize), totalSamples - samplesRendered));
            buffer.clear();
            midiBuffer.clear();

            renderGraph.processBlock(buffer, midiBuffer);
            renderTransport.advance(numThisBlock);

            writer->writeFromAudioSampleBuffer(buffer, 0, numThisBlock);

            samplesRendered += numThisBlock;
            ++blocksDone;
            if (onProgress)
            {
                float prog = static_cast<float>(blocksDone) / static_cast<float>(totalBlocks);
                onProgress(prog);
            }
        }

        delete writer;

        if (cancelFlag.load())
        {
            outputPath.deleteFile();
            message = "Export cancelled.";
        }
        else
        {
            message = "Export complete.";
            success = true;
        }
    }

finish:
    renderGraph.releaseResources();
    active = false;

    if (onComplete)
        onComplete(success, message);
}

} // namespace HDAW
