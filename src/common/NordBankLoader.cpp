#include "NordBankLoader.h"

#include "../engine/AudioEngine.h"
#include "../common/ProjectCommands.h"
#include "../mcp/PresetFileParser.h"   // pure Nord parsing (juce_core only — no MCP types)

#include <vector>

#include <juce_core/juce_core.h>

namespace HDAW {

NordBankLoadResult loadNordBankFile(AudioEngine& engine, int trackIndex, int slotIndex,
                                    const QString& path, int program, bool captureToTree)
{
    NordBankLoadResult res;

    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
    {
        res.error = "file not found: " + path;
        return res;
    }
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
    {
        res.error = "failed to read file";
        return res;
    }
    const auto suffix = f.getFileExtension().toLowerCase();
    // Normalize to complete F0..F7 dumps (payload coordinates differ between containers; see
    // PresetFileParser.h for the wire format).
    std::vector<std::vector<uint8_t>> dumps;
    if (suffix == ".syx")
    {
        const auto* b = static_cast<const uint8_t*>(block.getData());
        if (mcp::splitNordSyx(b, block.getSize(), dumps) < 0)
        {
            res.error = "truncated SysEx (missing F7)";
            return res;
        }
    }
    else if (suffix == ".mid")
    {
        juce::MemoryInputStream in(block, false);
        juce::MidiFile mf;
        if (!mf.readFrom(in))
        {
            res.error = "invalid .mid file";
            return res;
        }
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            for (int ev = 0; ev < seq->getNumEvents(); ++ev)
            {
                const auto metadata = seq->getEventPointer(ev);
                if (!metadata->message.isSysEx())
                    continue;
                const auto* raw = metadata->message.getRawData();
                dumps.emplace_back(raw, raw + metadata->message.getRawDataSize());
            }
        }
    }
    else
    {
        res.error = "unsupported file type (use .syx or .mid)";
        res.environmentFailure = false;   // a caller-argument problem
        return res;
    }
    if (dumps.empty())
    {
        res.error = "no sysex data found in file";
        return res;
    }

    // Validate EVERY dump before queueing anything (no partial bank loads).
    size_t totalBytes = 0;
    for (const auto& d : dumps)
    {
        if (auto err = mcp::validateNordDump(d.data(), d.size()); !err.isEmpty())
        {
            res.error = "invalid Nord dump: " + QString::fromStdString(err.toStdString());
            return res;
        }
        totalBytes += d.size();
    }
    if (program > 127 || program < -1)
    {
        res.error = "program must be 0..127";
        res.environmentFailure = false;   // a caller-argument problem
        return res;
    }

    ProjectCommands::FxMidiParams p;
    p.trackIndex = trackIndex;
    p.slotIndex = slotIndex;
    p.captureToTree = captureToTree;
    for (const auto& d : dumps)
    {
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = d;
        p.events.push_back(std::move(ev));
    }
    if (program >= 0)
    {
        // Voice selection AFTER the bank dumps land (BUG-7 plan step 4).
        ProjectCommands::FxMidiEvent pc;
        pc.kind = ProjectCommands::FxMidiEvent::Kind::ProgramChange;
        pc.channel = 1;
        pc.data1 = program;
        p.events.push_back(std::move(pc));
    }
    {
        // Capture-race protocol: append a harmless CC125 (undefined on the NL2x) at the END of the
        // batch so the trailing slot state is inert. Delivery is paced by the slot drain (<=1 SysEx
        // per block, order preserved) and the deferred state capture is delayed ~30ms per queued
        // SysEx (see sendFxMidi), then confirmed via get_fx_capture_status.
        ProjectCommands::FxMidiEvent cc;
        cc.kind = ProjectCommands::FxMidiEvent::Kind::ControlChange;
        cc.channel = 1;
        cc.data1 = 125;
        cc.data2 = 0;
        p.events.push_back(std::move(cc));
    }
    const auto r = engine.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
    {
        res.error = QString::fromStdString(r.error);
        return res;
    }

    res.ok = true;
    res.queued = r.queued;
    res.totalBytes = static_cast<int>(totalBytes);
    res.program = program;
    res.capturedToTree = r.capturedToTree;
    return res;
}

} // namespace HDAW
