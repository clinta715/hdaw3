#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class ProjectCommands;
class QJsonValue;
class QString;

namespace juce { class ValueTree; }

namespace frontend {
// `trackList` is the project's TRACK_LIST node, passed in for the B2 stable-id
// arguments (`trackID` / `folderID` next to the positional `trackId` /
// `folderId`): resolving an id needs the tree, and ProjectCommands exposes only
// index-based operations — the same reason dispatchRead receives its busList
// (and read.getWaveformPeaks is special-cased in FrontendRouter). One call site.
DispatchResult dispatchProject(ProjectCommands& cmds, const juce::ValueTree& trackList,
                               const QString& subMethod, const QJsonValue& params);
// The two project routes that need engine context (the ProjectModel) rather than
// just the command interface: removeTrack's dryRun/force guard and the
// addTrackWithFx composite. FrontendRouter routes those methods here before
// dispatchProject (see the definitions in Router_Project.cpp).
DispatchResult dispatchRemoveTrack(AudioEngine& engine, const QJsonValue& params);
DispatchResult dispatchAddTrackWithFx(AudioEngine& engine, const QJsonValue& params);
// project.setFxSidechain also needs engine context: the command lives on the
// concrete AudioEngineCommands (Track-FX-slot compressor sidechain v1) and not
// on the abstract ProjectCommands this file otherwise receives. Runs the SAME
// shared reader/body as the MCP set_fx_sidechain tool
// (src/common/FxSidechain.h) — identical payload and refusals.
DispatchResult dispatchSetFxSidechain(AudioEngine& engine, const QJsonValue& params);
// project.endBatch also needs engine context: its optional verify hook runs the
// shared render→verdict path (common/BatchEnd.h).
DispatchResult dispatchEndBatch(AudioEngine& engine, const QJsonValue& params);
DispatchResult dispatchSettings(AudioEngine& engine, const QString& subMethod,
                               const QJsonValue& params);
}
