#pragma once

#include <QtGlobal>

// QSettings key constants shared between UI and engine.
// Kept header-only so both PreferencesDialog and AudioEngine can use them
// without pulling Qt Widgets into the engine lib.

namespace SettingsKeys
{
    inline constexpr auto kKeyAudioDriver       = "audio/driverType";
    inline constexpr auto kKeyAudioOutputDevice  = "audio/outputDevice";
    inline constexpr auto kKeyAudioInputDevice   = "audio/inputDevice";
    inline constexpr auto kKeyAudioSampleRate    = "audio/sampleRate";
    inline constexpr auto kKeyAudioBufferSize    = "audio/bufferSize";

    // MCP HTTP
    inline constexpr auto kKeyMcpHttpEnabled     = "mcp/httpEnabled";
    inline constexpr auto kKeyMcpHttpHost        = "mcp/httpHost";
    inline constexpr auto kKeyMcpHttpPort        = "mcp/httpPort";

    inline constexpr const char* kDefaultMcpHttpHost = "127.0.0.1";
    inline constexpr quint16 kDefaultMcpHttpPort = 18765;

    // Backup
    inline constexpr auto kKeyMaxBackups         = "backup/maxBackups";

    // Plugin
    inline constexpr auto kKeyPluginIsolation    = "plugin/isolationEnabled";
    inline constexpr auto kKeyWatchPlugins       = "plugin/watchPlugins";

    // MIDI
    inline constexpr auto kKeyMidiDevice         = "midi/openDevice";

    // Project defaults
    inline constexpr auto kKeyDefaultTempo       = "project/defaultTempo";
    inline constexpr auto kKeyDefaultTimeSigNum  = "project/defaultTimeSigNumerator";
    inline constexpr auto kKeyDefaultTimeSigDen  = "project/defaultTimeSigDenominator";
}
