#pragma once

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
}
