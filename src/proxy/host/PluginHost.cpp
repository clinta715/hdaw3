#include "PluginHost.h"
#include "proxy/ProxyRingBuffer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>

PluginHost::PluginHost(uint32_t id, const std::string& pipe,
                       const std::string& shm, const std::string& plugin)
    : slotId(id), pipeName(pipe), shmName(shm), pluginPath(plugin),
      pipe(pipeName)
{
    formatManager.addFormat(new juce::VST3PluginFormat());
}

PluginHost::~PluginHost() {
    running.store(false);
    if (controlThread.joinable()) controlThread.join();
    if (audioThread.joinable()) audioThread.join();
}

int PluginHost::run() {
    if (!pipe.start()) return 1;
    if (!shm.create(shmName, proxy::computeShmSize(2, 512))) return 1;

    proxy::ProxyResponse readyResp{};
    readyResp.type = proxy::MessageType::READY;
    readyResp.result = 1;
    if (!pipe.send(readyResp)) return 1;

    controlThread = std::thread(&PluginHost::controlLoop, this);

    if (!loadPlugin()) return 1;

    audioThread = std::thread(&PluginHost::audioLoop, this);

    while (running.load()) {
        proxy::ProxyMessage msg{};
        if (!pipe.receive(msg)) {
            running.store(false);
            break;
        }

        switch (msg.type) {
            case proxy::MessageType::SHUTDOWN:
                running.store(false);
                break;

            case proxy::MessageType::SET_STATE: {
                if (plugin && msg.dataSize > 0) {
                    plugin->setStateInformation(msg.data, static_cast<int>(msg.dataSize));
                }
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_STATE;
                resp.result = 1;
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::GET_STATE: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_STATE_RESULT;
                if (plugin) {
                    juce::MemoryBlock block;
                    plugin->getStateInformation(block);
                    resp.dataSize = static_cast<uint32_t>(block.getSize());
                    std::memcpy(resp.data, block.getData(),
                                std::min(static_cast<size_t>(resp.dataSize), sizeof(resp.data)));
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::SET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_PARAM;
                resp.result = 1;
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_RESULT;
                resp.result = plugin ? 1 : 0;
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM_COUNT: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_COUNT_RESULT;
                if (plugin) {
                    uint32_t count = static_cast<uint32_t>(plugin->getNumParameters());
                    std::memcpy(resp.data, &count, sizeof(count));
                    resp.dataSize = sizeof(count);
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM_INFO: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_INFO_RESULT;
                resp.result = plugin ? 1 : 0;
                pipe.send(resp);
                break;
            }

            case proxy::MessageType::SHOW_EDITOR:
                editorVisible.store(true);
                { proxy::ProxyResponse r{}; r.type = proxy::MessageType::SHOW_EDITOR; r.result = 1; pipe.send(r); }
                break;

            case proxy::MessageType::CLOSE_EDITOR:
                editorVisible.store(false);
                { proxy::ProxyResponse r{}; r.type = proxy::MessageType::CLOSE_EDITOR; r.result = 1; pipe.send(r); }
                break;

            case proxy::MessageType::HEARTBEAT: {
                auto* hdr = shm.getHeader();
                if (hdr) hdr->childAlive.store(static_cast<uint32_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
                proxy::ProxyResponse r{};
                r.type = proxy::MessageType::HEARTBEAT;
                r.result = 1;
                pipe.send(r);
                break;
            }

            default:
                break;
        }
    }

    return 0;
}

void PluginHost::controlLoop() {
}

void PluginHost::audioLoop() {
    auto* hdr = shm.getHeader();
    if (!hdr || !plugin) return;

    hdr->numChannels = 2;
    hdr->blockSize = 512;
    hdr->sampleRate = 44100;

    juce::AudioBuffer<float> inputBuffer(2, 512);
    juce::AudioBuffer<float> outputBuffer(2, 512);
    juce::MidiBuffer midiBuffer;

    while (running.load()) {
        uint32_t cap = hdr->capacity;
        uint32_t r = hdr->inputReadPos.load(std::memory_order_relaxed);
        uint32_t w = hdr->inputWritePos.load(std::memory_order_acquire);

        if (w - r >= 1024) {
            float* inRing = shm.getInputRing();
            float* outRing = shm.getOutputRing();

            for (int ch = 0; ch < 2; ++ch) {
                for (int s = 0; s < 512; ++s)
                    inputBuffer.setSample(ch, s, inRing[(r + ch * 512 + s) & (cap - 1)]);
            }
            hdr->inputReadPos.store(r + 1024, std::memory_order_release);

            midiBuffer.clear();
            plugin->processBlock(inputBuffer, midiBuffer);

            uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
            for (int ch = 0; ch < 2; ++ch) {
                for (int s = 0; s < 512; ++s)
                    outRing[(ow + ch * 512 + s) & (cap - 1)] = inputBuffer.getSample(ch, s);
            }
            hdr->outputWritePos.store(ow + 1024, std::memory_order_release);
        } else {
            std::this_thread::yield();
        }
    }
}

bool PluginHost::loadPlugin() {
    return loadPluginByPath(juce::String(pluginPath));
}

bool PluginHost::loadPluginByPath(const juce::String& path) {
    juce::String error;

    for (auto* fmt : formatManager.getFormats()) {
        if (fmt->fileMightContainThisPluginType(path)) {
            juce::PluginDescription desc;
            desc.fileOrIdentifier = path;
            desc.pluginFormatName = fmt->getName();

            plugin = formatManager.createPluginInstance(desc, 44100.0, 512, error);
            if (plugin) {
                pluginLoaded.store(true);
                return true;
            }
        }
    }

    return false;
}
