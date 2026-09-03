#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace juce { class String; class ValueTree; }
class AudioEngine;

namespace mcp {
class McpServer;

QString jstr(const juce::String& s);
QJsonObject objSchema(const QJsonObject& props, const QJsonArray& required = {});
juce::ValueTree findClip(AudioEngine* e, int clipId, int* outTrackIdx);
juce::ValueTree findNote(AudioEngine* e, int noteId, int* outClipId);
juce::ValueTree findCcPoint(AudioEngine* e, int ccId, int* outClipId);
juce::ValueTree findLane(AudioEngine* e, int trackId, const QJsonValue& ref);

void registerReadTools(McpServer& s, AudioEngine* e);
void registerTrackTools(McpServer& s, AudioEngine* e);
void registerClipTools(McpServer& s, AudioEngine* e);
void registerNoteTools(McpServer& s, AudioEngine* e);
void registerCcTools(McpServer& s, AudioEngine* e);
void registerTimingTools(McpServer& s, AudioEngine* e);
void registerGenerateTools(McpServer& s, AudioEngine* e);
void registerInstrumentTools(McpServer& s, AudioEngine* e);
void registerPatternTools(McpServer& s, AudioEngine* e);

void registerCompositionTools(McpServer& s, AudioEngine* e);
void registerArrangerTools(McpServer& s, AudioEngine* e);
void registerProjectSaveLoadTools(McpServer& s, AudioEngine* e);

void registerAudioReadTools(McpServer& s, AudioEngine* e);
void registerFxTools(McpServer& s, AudioEngine* e);
void registerMidiFxTools(McpServer& s, AudioEngine* e);
void registerModulationTools(McpServer& s, AudioEngine* e);
void registerAutomationTools(McpServer& s, AudioEngine* e);
void registerSendTools(McpServer& s, AudioEngine* e);
void registerEnvelopeTools(McpServer& s, AudioEngine* e);
void registerProjectDomain(McpServer& s, AudioEngine* e);
void registerTransportDomain(McpServer& s, AudioEngine* e);
void registerAudioDomain(McpServer& s, AudioEngine* e);
void registerFxSlotTools(McpServer& s, AudioEngine* e);
void registerFxPresetTools(McpServer& s, AudioEngine* e);
void registerFxChainTools(McpServer& s, AudioEngine* e);
void registerSamplerTools(McpServer& s, AudioEngine* e);
void registerFmSynthTools(McpServer& s, AudioEngine* e);
void registerPsyFmTools(McpServer& s, AudioEngine* e);
void registerSessionDomain(McpServer& s, AudioEngine* e);
void registerLibraryDomain(McpServer& s, AudioEngine* e);
void registerSettingsDomain(McpServer& s, AudioEngine* e);
void registerTuningTools(McpServer& s, AudioEngine* e);

} // namespace mcp
