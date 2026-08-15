# Audio Device Preferences — Persist & Restore

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create the implementation plan.

**Goal:** Persist audio device settings (driver type, output/input device, sample rate, buffer size) to QSettings and restore them on startup, so users don't have to reconfigure every launch.

**Architecture:** Add QSettings keys for audio device properties. Save them when the user applies device changes in PreferencesDialog. Restore them in AudioEngine::initialize() after the initial default device setup. No new UI — the existing combo boxes in PreferencesDialog already work.

**Tech Stack:** Qt 6 QSettings, JUCE AudioDeviceManager

---

## Current State

The PreferencesDialog already has a working audio device section with combo boxes for driver type, output device, input device, sample rate, and buffer size. Changes apply immediately via `dm.setAudioDeviceSetup()`. However, nothing is persisted — device settings reset to OS defaults on every launch.

## Design

### QSettings Keys

Add these constants to `PreferencesDialog.h`:

| Constant | QSettings key | Type | Default |
|----------|--------------|------|---------|
| `kKeyAudioDriver` | `"audio/driverType"` | `QString` | (empty = OS default) |
| `kKeyAudioOutputDevice` | `"audio/outputDevice"` | `QString` | (empty = OS default) |
| `kKeyAudioInputDevice` | `"audio/inputDevice"` | `QString` | (empty = OS default) |
| `kKeyAudioSampleRate` | `"audio/sampleRate"` | `int` | `0` (OS default) |
| `kKeyAudioBufferSize` | `"audio/bufferSize"` | `int` | `0` (OS default) |

### Save Path

In `PreferencesDialog::onApply()` and `onSave()`, after the existing non-audio settings are persisted, also write the current audio device settings:

```cpp
auto& dm = engine->getDeviceManager();
auto setup = dm.getAudioDeviceSetup();
auto s = settings();
s.setValue(kKeyAudioDriver, driverCombo->currentText());
s.setValue(kKeyAudioOutputDevice, setup.outputDeviceName);
s.setValue(kKeyAudioInputDevice, setup.inputDeviceName);
s.setValue(kKeyAudioSampleRate, setup.sampleRate);
s.setValue(kKeyAudioBufferSize, setup.bufferSize);
```

### Restore Path

In `AudioEngine::initialize()`, after `initialiseWithDefaultDevices(2, 2)`, attempt to restore saved settings:

```cpp
QSettings s;
QString savedDriver = s.value(kKeyAudioDriver).toString();
QString savedOutput = s.value(kKeyAudioOutputDevice).toString();
QString savedInput  = s.value(kKeyAudioInputDevice).toString();
int savedRate       = s.value(kKeyAudioSampleRate, 0).toInt();
int savedBuffer     = s.value(kKeyAudioBufferSize, 0).toInt();

if (!savedDriver.isEmpty() || !savedOutput.isEmpty())
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup = deviceManager.getAudioDeviceSetup();

    if (!savedDriver.isEmpty())
        deviceManager.setCurrentAudioDeviceType(savedDriver, true);

    if (!savedOutput.isEmpty())
        setup.outputDeviceName = savedOutput;
    if (!savedInput.isEmpty())
        setup.inputDeviceName = savedInput;
    if (savedRate > 0)
        setup.sampleRate = savedRate;
    if (savedBuffer > 0)
        setup.bufferSize = savedBuffer;

    auto err = deviceManager.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty())
    {
        // Fallback: saved device not available, use defaults
        deviceManager.initialiseWithDefaultDevices(2, 2);
    }
}
```

### Startup Flow

1. `initialiseWithDefaultDevices(2, 2)` — establishes a working device (always succeeds)
2. Read QSettings for saved device preferences
3. If any saved preference exists, try `setAudioDeviceSetup()` with the saved values
4. If that fails (device unplugged, driver changed), fall back to the default device (already initialized)
5. JUCE calls `prepareToPlay()` with the actual device parameters, which triggers routing graph rebuild

### Edge Cases

- **Saved device no longer available:** `setAudioDeviceSetup()` returns an error string. Catch it, log it, keep the default device.
- **First launch (no saved settings):** QSettings returns empty strings / 0. The `if (!savedDriver.isEmpty() || !savedOutput.isEmpty())` guard skips restoration.
- **Driver type change:** Must call `setCurrentAudioDeviceType()` before setting device names, since device names are driver-specific.
- **Sample rate not available on device:** JUCE clamps to the nearest available rate. No explicit handling needed.
