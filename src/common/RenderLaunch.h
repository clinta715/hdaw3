#pragma once
// The ONE shared render-launcher behind every surface that runs a project
// through the export path: the MCP `export_audio` tool and the RPC
// `export.audio` route (both launch asynchronously and report progress), and
// the `verify_window` / `render_and_verify` pair (which launch and WAIT,
// because their contract is measure-and-answer).
//
// WHY THIS EXISTS (S4 of docs/plans/2026-09-28-agent-mechanization.md): the
// launch sequence — the in-flight export guard's counterpart callers, the
// TREE COPY (an offline render must never write the live tree, lesson 27), the
// optional `trackIds` filter, and ExportManager::startExport — was hand-copied
// in each surface. Any fix to it (a new copy-time guard, a new filter rule)
// could land in one copy and silently miss the others. The launcher is also
// what keeps `verify_window` honest: it renders the WHOLE project through the
// EXACT path export_audio uses, so the measurement sees the same sum-peak
// clamps the full render carries (windowed renders do not — see
// docs/handoffs/2026-09-28-v0.39.2-backlog-closeout.md §3).
//
// READ-ONLY with respect to the live project: only the offline tree copy and
// the ExportManager are touched. No DSP, no graph mutation, no undo.

#include <QString>

#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include <juce_core/juce_core.h>

#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/PluginManager.h"
#include "../engine/ProjectPool.h"
#include "../model/ProjectModel.h"

namespace HDAW {

struct ProjectRenderLaunch
{
    bool started = false;
    QString error;   // non-empty when !started
};

// The export_audio `trackIds` filter, applied to an OFFLINE render copy only:
// every non-kept track is muted + zeroed with solos cleared, so a kept track
// plays regardless of the live project's solo state. Empty `trackIds` = no-op
// (the whole project renders). `trackIds` are the STABLE trackID values
// (`list_tracks` exposes them; position i carries trackID i+1 by design, so
// the values never coincide). Shared so the tool and the route cannot drift.
inline void applyTrackFilterToRenderCopy(juce::ValueTree& projectCopy,
                                         const std::vector<int>& trackIds)
{
    if (trackIds.empty())
        return;
    auto trackList = projectCopy.getChildWithName(IDs::TRACK_LIST);
    if (!trackList.isValid())
        return;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        bool keep = false;
        const int trackID = static_cast<int>(trackList.getChild(i)
                                                 .getProperty(IDs::trackID, 0));
        for (int id : trackIds)
            if (id == trackID) { keep = true; break; }
        auto tr = trackList.getChild(i);
        tr.setProperty(IDs::isSoloed, false, nullptr);
        tr.setProperty(IDs::isMuted, !keep, nullptr);
        if (!keep)
            tr.setProperty(IDs::volume, 0.0, nullptr);
    }
}

// Launch a render of the project (or [startTime, startTime + duration)) to
// `outputPath`, through ExportManager, on a fresh tree copy. `onProgress` /
// `onComplete` are installed BEFORE the render thread starts (a null callback
// means "this caller does not want that notification"). Returns started=false
// with a reason when the engine is not initialized or the export could not
// start; the callbacks are cleared in that case.
//
// The caller owns the busy-check (`isExporting`): export_audio queues behind a
// prior render with its own wait policy, so that decision is not shared.
inline ProjectRenderLaunch launchProjectRender(
    AudioEngine& engine,
    const QString& outputPath,
    double sampleRate,
    int bitDepth,
    ExportManager::Format format,
    double startTime,
    double duration,
    const std::vector<int>& trackIds = {},
    std::function<void(float)> onProgress = nullptr,
    std::function<void(bool, const juce::String&)> onComplete = nullptr)
{
    ProjectRenderLaunch out;

    auto* mainProc = engine.getMainProcessor();
    if (mainProc == nullptr)
    {
        out.error = "audio engine not initialized";
        return out;
    }

    juce::File outFile(juce::String(outputPath.toUtf8().constData()));
    if (outFile.existsAsFile())
        outFile.deleteFile();

    // Offline render copy: the live project is never mutated by a render.
    juce::ValueTree projectCopy = engine.getProjectModel().getTree().createCopy();

    // Validate BEFORE starting any filtered render — all-or-nothing: one
    // unknown id refuses the whole list (no partial filter, no render).
    // trackIds are STABLE trackID values; a legacy positional id (e.g. 0 —
    // trackIDs start at 1) used to silently export the WRONG track.
    if (!trackIds.empty())
    {
        auto trackList = projectCopy.getChildWithName(IDs::TRACK_LIST);
        if (!trackList.isValid())
        {
            out.error = "project has no track list";
            return out;
        }
        std::set<int> known;
        const int numTracks = trackList.getNumChildren();
        for (int i = 0; i < numTracks; ++i)
        {
            const int id = static_cast<int>(trackList.getChild(i)
                                                .getProperty(IDs::trackID, 0));
            // A malformed/legacy track node without a trackID must not mint a
            // phantom id 0: `[0]` stays refused and the refusal's existing-ids
            // list stays honest (trackIDs are floor-1 by contract).
            if (id > 0)
                known.insert(id);
        }
        juce::String unknown;
        for (int id : trackIds)
            if (known.count(id) == 0)
                unknown += (unknown.isEmpty() ? "" : ", ") + juce::String(id);
        if (unknown.isNotEmpty())
        {
            // Name the ACTUAL existing ids — they are non-contiguous after
            // removals, so a 1..N range would lie. Cap the list (same honesty
            // convention as the O1 device lister).
            juce::String existing;
            int listed = 0;
            for (int id : known)
            {
                if (listed == 12) { existing += ", …"; break; }
                existing += (existing.isEmpty() ? "" : ", ") + juce::String(id);
                ++listed;
            }
            const QString unknownList = QString::fromUtf8(unknown.toRawUTF8());
            const QString existingList = QString::fromUtf8(existing.toRawUTF8());
            out.error = QString("unknown trackID(s) %1 — trackIds are the "
                                "stable trackID values exposed by list_tracks "
                                "(existing: %2); nothing was rendered")
                            .arg(unknownList)
                            .arg(existingList);
            return out;
        }
    }

    applyTrackFilterToRenderCopy(projectCopy, trackIds);

    auto& em = mainProc->getExportManager();
    auto& formatManager = engine.getProjectPool().getFormatManager();
    auto* pluginManager = &engine.getPluginManager();

    em.onProgress = std::move(onProgress);
    em.onComplete = std::move(onComplete);

    if (!em.startExport(projectCopy, formatManager, pluginManager, outFile,
                        sampleRate, startTime, duration, format, bitDepth))
    {
        em.onProgress = nullptr;
        em.onComplete = nullptr;
        out.error = "failed to start export";
        return out;
    }

    out.started = true;
    return out;
}

struct ProjectRenderOutcome
{
    bool ok = false;
    QString error;          // non-empty on failure
    juce::String message;   // ExportManager's last result message
};

// launchProjectRender + a BOUNDED WAIT for the render to finish, then the
// render thread's own verdict (getLastExportMessage: anything that does not
// start with "Export complete" is a FAILURE, never a silent success — the
// `export-dir-must-exist` trap). The wait is safe off the message thread: the
// process owns a dedicated JUCE message pump (common/MessagePumpThread.h) that
// services the render-graph bake; this function must NOT be called ON the pump
// thread.
//
// On timeout the render is cancelled and joined (mirroring
// AudioEngineCommands' renderTrackWindow) and the error names the budget.
// `timeoutMs` default 600000.
inline ProjectRenderOutcome renderProjectAndWait(
    AudioEngine& engine,
    const QString& outputPath,
    double sampleRate,
    int bitDepth,
    ExportManager::Format format,
    double startTime,
    double duration,
    const std::vector<int>& trackIds = {},
    uint32_t timeoutMs = 600000)
{
    ProjectRenderOutcome out;

    auto* mainProc = engine.getMainProcessor();
    if (mainProc == nullptr)
    {
        out.error = "audio engine not initialized";
        return out;
    }
    auto& em = mainProc->getExportManager();
    if (em.isExporting())
    {
        out.error = "another export is in progress";
        return out;
    }

    auto launch = launchProjectRender(engine, outputPath, sampleRate, bitDepth, format,
                                      startTime, duration, trackIds, nullptr, nullptr);
    if (!launch.started)
    {
        out.error = launch.error;
        return out;
    }

    if (!em.waitForIdle(timeoutMs))
    {
        em.cancelAndJoin();
        out.error = QString("render timed out after %1ms").arg(timeoutMs);
        return out;
    }

    out.message = em.getLastExportMessage();
    if (!out.message.startsWith("Export complete"))
    {
        out.error = QString("export failed: %1")
                        .arg(QString::fromUtf8(out.message.toRawUTF8()));
        return out;
    }

    out.ok = true;
    return out;
}

} // namespace HDAW
