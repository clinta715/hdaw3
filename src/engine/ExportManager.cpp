#include "ExportManager.h"
#include "PluginManager.h"
#include "../proxy/PluginProxySlot.h"
#include "../common/DebugLog.h"
#include <cstdlib>

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
    {
        HDAW_LOG("Export", "startExport rejected: a previous export is still active");
        return false;
    }

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
    proxy::setRenderCancelRequested(true);
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

uint32_t ExportManager::computeBakeWaitMs(const juce::ValueTree& projectTree)
{
    uint32_t totalClips = 0;
    auto trackList = projectTree.getChildWithName(IDs::TRACK_LIST);
    if (trackList.isValid())
    {
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
            if (clipList.isValid())
                totalClips += static_cast<uint32_t>(clipList.getNumChildren());
        }
    }

    const uint32_t scaled = 10000u + 50u * totalClips;
    return static_cast<uint32_t>((std::max)(15000u, (std::min)(120000u, scaled)));
}

void ExportManager::renderThreadFunc(juce::ValueTree treeCopy,
                                     juce::AudioFormatManager* formatManager,
                                     PluginManager* pluginManager, juce::File outputPath,
                                     double sampleRate, double startTime, double duration,
                                     Format format, int bitDepth)
{
    bool success = false;
    juce::String message;

    proxy::setRenderMode(true);
    proxy::setRenderCancelRequested(false);

    // RAII `active` guard: clear the in-flight flag on EVERY exit path of
    // this thread — normal completion, the `goto finish` bail-outs, and any
    // exception thrown before/during/after the render-graph scope (e.g. a
    // wedged proxy that makes the render crawl, or a throw in
    // createOfflineCopy below). Previously `active` was cleared only in the
    // function tail, so a thread that threw outside the try/catch (or never
    // reached the tail) permanently wedged the exporter: every subsequent
    // startExport() returned false with no recovery short of killing the
    // process. Declared at function scope so reverse destruction order keeps
    // it alive across every path; it fires as the thread unwinds, after
    // onComplete has run.
    struct ActiveGuard {
        std::atomic<bool>& flag;
        explicit ActiveGuard(std::atomic<bool>& f) : flag(f) {}
        ~ActiveGuard() { flag = false; }
    } activeGate(active);

    // Layer 1: the export render graph gets its OWN plugin domain. Passing
    // the live PluginManager into the render RoutingManager shared the live
    // ProxyProcessManager (children map, slot crash callbacks, health
    // monitor, nextProxySlotId), the live CrashRecoveryManager, and the live
    // liveProxySlots registry with the export. A live FX-chain rebuild during
    // export could then spawn a host whose defensive killPluginHost(slotId)
    // killed the export's own child and replaced its shm — the wedged-export
    // root cause. The dedicated PluginManager is seeded from the live one
    // (plugin list / blacklist / preset cache, in memory — no scan, no disk
    // cache IO) so createPluginInstance / resolveIdentifierToPath work
    // offline, and its ProxyProcessManager gets its own OS name namespace
    // (pipes/shm/crash-state files) so its slot ids can never collide with
    // live slots even though both counters start at 1. The "export_" domain
    // label below is made UNIQUE PER OFFLINE COPY by setProxyNamespacePrefix
    // → makeUniqueNamespacePrefix ("export_<pidhex>_<n>_"), so overlapping
    // exports can never share a static "export_" domain — a stale engine's
    // children could otherwise hold every export slot's pipe/shm names. Its
    // health monitor is never started (createPluginInstance skips it for
    // offline domains) and its crash callbacks land only in its own registry.
    //
    // Declared BEFORE the render-graph scope so C++ reverse destruction
    // order guarantees it outlives every ~PluginProxySlot (which calls back
    // into its ProxyProcessManager during renderGraph teardown), and
    // destroyed before render mode is cleared below.
    std::unique_ptr<HDAW::PluginManager> exportPluginManager;
    if (pluginManager != nullptr)
    {
        exportPluginManager = HDAW::PluginManager::createOfflineCopy(*pluginManager);
        exportPluginManager->setProxyNamespacePrefix("export_");
    }

    // Suppress crash-recovery respawn for the ENTIRE export duration,
    // including teardown. A crashed plugin during offline export should
    // fail the export, not respawn into a half-rendered file; and with
    // respawn suppressed there is no kill+swap to race the export
    // render thread's processBlock.
    //
    // This RAII guard is declared BEFORE `renderGraph` so C++ reverse
    // destruction order guarantees its destructor runs AFTER renderGraph's
    // — i.e. AFTER every ~PluginProxySlot has fired its destruction
    // callback (erasing from the dedicated domain's liveProxySlots +
    // cancel()ing the recovery entry). By the time respawnEnabled is
    // restored to true, all of export's proxies are gone and their entries
    // canceled, so a Timer tick can never dereference a freed export proxy.
    //
    // NOTE: the guard targets the DEDICATED export domain's
    // CrashRecoveryManager, not the live one's — with per-domain plugin
    // machinery the live graph's crash recovery can no longer touch export
    // children, so it keeps running during export.
    //
    // NOTE: the callers (McpExportTool, FrontendRouter) drive the ENGINE's
    // member ExportManager, whose ~ExportManager only runs at engine
    // teardown — so a pure join-based restore would leave respawnEnabled
    // false between exports. The RAII guard closes that gap: it fires the
    // instant this render thread finishes unwinding.
    HDAW::CrashRecoveryManager* recoveryMgr =
        exportPluginManager ? &exportPluginManager->recovery() : nullptr;
    struct RespawnSuppressionGuard {
        HDAW::CrashRecoveryManager* crm;
        explicit RespawnSuppressionGuard(HDAW::CrashRecoveryManager* c) : crm(c) {
            if (crm) crm->respawnEnabled.store(false, std::memory_order_relaxed);
        }
        ~RespawnSuppressionGuard() {
            if (crm) crm->respawnEnabled.store(true, std::memory_order_relaxed);
        }
    } respawnGate(recoveryMgr);

    // NOTE: Plugin isolation stays enabled. The export uses the full
    // AudioProcessorGraph pipeline (graph.processBlock) so CLAP instruments
    // run in isolated child processes just like live playback.

    // Scope the entire render pipeline so the render graph — including the
    // baked render sequence whose Node::Ptr references keep every Track (and
    // its CLAP instances) alive — is destroyed BEFORE render mode is cleared.
    // ~CLAPPluginInstance → deactivate() must run on the host's reported main
    // thread (the render thread while render mode is set); thread-checking
    // plugins (Odin2) std::terminate otherwise. AudioProcessorGraph::clear()
    // alone is insufficient: a rebuild of the render sequence only happens on
    // the JUCE message thread, so the sequence's node references outlive the
    // graph's own nodes map and die in ~AudioProcessorGraph.
    {
        juce::AudioProcessorGraph renderGraph;

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
    
            // Note: no DecodedSoundPool here — the export render thread must
            // not touch the live (message-thread) pool; clips decode directly.
            RoutingManager routingManager(renderGraph, localModel, *formatManager,
                                          renderTransport,
                                          exportPluginManager ? exportPluginManager.get()
                                                              : pluginManager);
            routingManager.setPlaybackInfo(sampleRate, blockSize);
            routingManager.rebuildFromValueTree();
    
            // Complete the topology BEFORE prepareToPlay. The master→IO
            // connections are already legal at this point: setBusesLayout ran
            // before rebuildFromValueTree, so the audioOutputNode picked up its
            // 2 input channels synchronously at addNode time. (The live graph in
            // MainAudioProcessor must reconnect AFTER prepareToPlay because its
            // bus layout arrives via device negotiation — that does not apply
            // here.) Ordering it this way makes prepareToPlay the final topology
            // change, so the single async render-sequence bake it triggers
            // contains the complete graph.
            routingManager.reconnectMasterToOutput();
    
            renderGraph.prepareToPlay(sampleRate, blockSize);

            // Offline render: wait for the async render-sequence bake (delivered
            // on the JUCE message pump thread) instead of racing it. Without
            // this, processBlock hits the audio.clear() fallback for every block
            // processed before the bake lands — the race that intermittently
            // produced fully-silent exports.
            renderGraph.setNonRealtime(true);

            // Switch clip sources to synchronous (non-realtime) streaming
            // BEFORE the first processBlock. The live graph was already built
            // by prepareToPlay above; the flag-alone route would only reach the
            // streamer on the next rebuild, so push through to every live
            // ClipSourceProcessor now. No audio thread is running yet, and the
            // streamer's background reader does plain AudioFormatReader I/O
            // (no JUCE message machinery), so stopping/joining it is safe here.
            routingManager.setClipSourcesNonRealtime(true);

            // Layer 2: bounded render-sequence bake wait. JUCE 8's
            // AudioProcessorGraph::processBlock in non-realtime mode spins
            // forever (Thread::sleep(1)) when no render sequence has been
            // baked; that spin cannot be interrupted from another thread, so
            // the FIRST processBlock must never be called until the bake has
            // landed. The bake is delivered by the JUCE message thread when it
            // services the graph's LockingAsyncUpdater. We detect it without
            // touching the graph's internals: post a probe message to the
            // message queue AFTER the graph's own updater messages (posted
            // during rebuildFromValueTree/prepareToPlay on this same render
            // thread — FIFO ordering), then wait a bounded deadline for the
            // probe to fire. When it fires, every message queued before it —
            // including the render-sequence bake — has been processed, so the
            // first processBlock can no longer spin. On timeout the export
            // FAILS with a clear message instead of hanging forever. The wait
            // budget scales with project clip count (computeBakeWaitMs below:
            // floor 15s, 50ms/clip, cap 120s); the HDAW_EXPORT_BAKE_TIMEOUT_MS
            // env override takes precedence.
            {
                // Heap-allocated with the message queue as SOLE owner: JUCE
                // deletes posted MessageBase objects after dispatch (the
                // ReferenceCountedArray holds the only refs), so the probe
                // must be a plain `new` — neither a stack object (deleted on
                // a stack address) nor a shared_ptr-owned object (deleted
                // while its control block lives) is valid here. The FLAG is
                // kept alive separately via shared_ptr: if the export times
                // out, the queued probe may still fire later, and the flag
                // must outlive the stack unwind.
                auto bakeLanded = std::make_shared<std::atomic<bool>>(false);
                struct BakeProbeMessage final : public juce::CallbackMessage {
                    std::shared_ptr<std::atomic<bool>> landed;
                    explicit BakeProbeMessage(std::shared_ptr<std::atomic<bool>> f)
                        : landed(std::move(f)) {}
                    void messageCallback() override
                    {
                        landed->store(true, std::memory_order_release);
                    }
                };
                auto* probe = new BakeProbeMessage(bakeLanded);
                probe->post();

                // The bake wait scales with project size: large graphs take
                // longer than the 15s floor (measured ~17-21s for a 771-clip
                // project), so computeBakeWaitMs raises the default with clip
                // count (50ms/clip, cap 120s). HDAW_EXPORT_BAKE_TIMEOUT_MS
                // overrides the default at this call site.
                uint32_t kMaxBakeWaitMs = ExportManager::computeBakeWaitMs(treeCopy);
                if (const char* envMs = std::getenv("HDAW_EXPORT_BAKE_TIMEOUT_MS"))
                {
                    const int parsed = juce::String(envMs).getIntValue();
                    if (parsed > 0)
                        kMaxBakeWaitMs = static_cast<uint32_t>(parsed);
                }
                const auto bakeDeadline =
                    juce::Time::getMillisecondCounter() + kMaxBakeWaitMs;
                while (!bakeLanded->load(std::memory_order_acquire)
                       && !cancelFlag.load()
                       && juce::Time::getMillisecondCounter() < bakeDeadline)
                {
                    juce::Thread::sleep(10);
                }

                if (!bakeLanded->load(std::memory_order_acquire))
                {
                    success = false;
                    message = cancelFlag.load()
                        ? "Export cancelled."
                        : "Render graph bake timed out after " + juce::String(kMaxBakeWaitMs) + "ms - export aborted.";
                    // Mirrors the cancel path: never leave a partial/zero-byte
                    // output file behind for a failed export.
                    outputPath.deleteFile();
                    goto finish;
                }
            }
    
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
                    int numThisBlock = static_cast<int>((std::min)(
                        static_cast<int64_t>(blockSize), totalSamples - samplesRendered));
    
                    buffer.clear();
                    midiBuffer.clear();
    
                    // Drive the full AudioProcessorGraph pipeline.
                    // This calls processBlock on all nodes in topological order:
                    // MidiClipProcessor → Track (with CLAP instruments) → MasterBus → AudioOutput.
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
        // Destroy the render graph's nodes BEFORE clearing render mode:
        // node destruction runs ~CLAPPluginInstance → deactivate(), which
        // thread-checking plugins (Odin2) require to happen on the host's
        // reported main thread (the render thread while render mode is
        // set). The render sequence's node references are dropped in
        // ~AudioProcessorGraph, which the scope close below runs before
        // render mode is cleared.
        renderGraph.clear();
    }

    proxy::setRenderCancelRequested(false);
    proxy::setRenderMode(false);

    if (onComplete)
        onComplete(success, message);
}

} // namespace HDAW
