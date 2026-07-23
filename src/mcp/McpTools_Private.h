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
juce::ValueTree findLane(AudioEngine* e, int trackId, const QJsonValue& ref);

void registerProjectDomain(McpServer& s, AudioEngine* e);
void registerTransportDomain(McpServer& s, AudioEngine* e);
void registerAudioDomain(McpServer& s, AudioEngine* e);

} // namespace mcp
