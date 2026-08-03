#include "ExportManager.h"
#include "../proxy/PluginProxySlot.h"
#include "../common/DebugLog.h"

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
            maxEnd = (std::max)(maxEnd, start + dur);
        }
    }

    return (std::max)(maxEnd + 3.0, 4.0); // at least 4 seconds, add 3s tail
}

void ExportManager::renderThreadFunc(juce::ValueTree treeCopy,
                                     juce::AudioFormatManager* formatManager,
                                     PluginManager* pluginManager, juce::File outputPath,
                                     double sampleRate, double startTime, double duration,
                                     Format format, int bitDepth)
{
    bool success = false;
    juce::String message;
    juce::AudioProcessorGraph renderGraph;

    proxy::setRenderMode(true);

    // Disable plugin isolation for offline render. The render graph is a
    // local, single-threaded context — crash isolation (out-of-process child)
    // adds IPC latency that breaks the tight render loop, and the render
    // doesn't need crash protection. Save and restore the original setting.
    bool wasIsolationEnabled = pluginManager && pluginManager->isolationEnabled;
    if (pluginManager) pluginManager->isolationEnabled = false;

    try
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

        renderGraph.setPlayHead(&renderPlayHead);

        const int blockSize = 512;

        // Propagate a stereo bus layout to the graph BEFORE rebuilding.
        // The graph's audioOutputNode reads its input-channel count from
        // the graph's own output bus; without this, the IO node reports
        // 0 channels and every master→IO addConnection is silently
        // rejected (no audio reaches the output buffer even though the
        // master meter moves). Must run BEFORE rebuildFromValueTree so
        // the IO node is created with the correct channel count.
        {
            juce::AudioProcessorGraph::BusesLayout renderLayout;
            renderLayout.inputBuses.add(juce::AudioChannelSet::stereo());
            renderLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            renderGraph.setBusesLayout(renderLayout);
        }

        RoutingManager routingManager(renderGraph, localModel, *formatManager,
                                      renderTransport, pluginManager);
        routingManager.setPlaybackInfo(sampleRate, blockSize);
        routingManager.rebuildFromValueTree();

        renderGraph.prepareToPlay(sampleRate, blockSize);
        routingManager.reconnectMasterToOutput();

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
                int numThisBlock = static_cast<int>((std::min)(static_cast<int64_t>(blockSize), totalSamples - samplesRendered));
                buffer.clear();
                midiBuffer.clear();

                renderGraph.processBlock(buffer, midiBuffer);
                renderTransport.advance(numThisBlock);

                if (!writer->writeFromAudioSampleBuffer(buffer, 0, numThisBlock))
                {
                    success = false;
                    message = "Disk write failed during export.";
                    delete writer;
                    delete outStream;
                    goto finish;
                }

                samplesRendered += numThisBlock;
                ++blocksDone;
                if (onProgress)
                {
                    float prog = static_cast<float>(blocksDone) / static_cast<float>(totalBlocks);
                    onProgress(prog);
                }
            }

            delete writer;
            // NOTE: do NOT also `delete outStream` here. The AudioFormatWriter
            // takes ownership of the output stream in its constructor and deletes
            // it in its destructor. A second `delete outStream` is undefined
            // behaviour and causes a hang in debug builds (MSVC debug heap walks
            // the freed block and stalls). The previous code was incorrect.

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
    }
    catch (const std::exception& e)
    {
        // The render worker runs on its own std::thread with no caller
        // catching exceptions; an uncaught throw here calls std::terminate
        // (process crash) and never sets `active=false` or fires `onComplete`,
        // leaving any waiting dispatchExport / MCP export caller blocked
        // forever on doneFuture.get(). Convert to a reported failure instead.
        success = false;
        message = "Export threw an exception: " + juce::String(e.what());
    }
    catch (...)
    {
        success = false;
        message = "Export threw an unknown exception.";
    }

finish:
    renderGraph.releaseResources();
    if (pluginManager) pluginManager->isolationEnabled = wasIsolationEnabled;
    proxy::setRenderMode(false);
    active = false;

    if (onComplete)
        onComplete(success, message);
}

} // namespace HDAW
