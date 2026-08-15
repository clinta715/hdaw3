# Audio Device Preferences — Persist & Restore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist audio device settings (driver type, output/input device, sample rate, buffer size) to QSettings and restore them on startup.

**Architecture:** Add 5 QSettings keys to `PreferencesDialog.h`. Save them in `PreferencesDialog::onApply()`. Restore them in `AudioEngine::initialize()` after the initial default device setup, with fallback to defaults on failure.

**Tech Stack:** Qt 6 QSettings, JUCE AudioDeviceManager

---

## File Structure

| File | Change |
|------|--------|
| `src/ui/PreferencesDialog.h` | Add 5 `kKeyAudio*` constants |
| `src/ui/PreferencesDialog.cpp` | Save audio device settings in `onApply()` |
| `src/engine/AudioEngine.cpp` | Restore saved device settings in `initialize()` |

---

## Task 1: Add QSettings key constants

**Files:**
- Modify: `src/ui/PreferencesDialog.h:42-60`

- [ ] **Step 1: Add the 5 audio device key constants**

After `kKeyPluginScanPaths` (line 60), before `kSettingsOrg` (line 61), add:

```cpp
    static inline constexpr auto kKeyAudioDriver = "audio/driverType";
    static inline constexpr auto kKeyAudioOutputDevice = "audio/outputDevice";
    static inline constexpr auto kKeyAudioInputDevice = "audio/inputDevice";
    static inline constexpr auto kKeyAudioSampleRate = "audio/sampleRate";
    static inline constexpr auto kKeyAudioBufferSize = "audio/bufferSize";
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/ui/PreferencesDialog.h
git commit -m "PreferencesDialog: add QSettings keys for audio device persistence"
```

---

## Task 2: Save audio device settings in onApply()

**Files:**
- Modify: `src/ui/PreferencesDialog.cpp:368-385` (the `onApply()` method)

- [ ] **Step 1: Add audio device save logic**

At the end of `onApply()`, before `emit preferencesApplied()` (line 384), add:

```cpp
    // Persist audio device settings
    if (audioEngine != nullptr)
    {
        auto& dm = audioEngine->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        settings.setValue(kKeyAudioDriver, deviceTypeCombo->currentText());
        settings.setValue(kKeyAudioOutputDevice, setup.outputDeviceName);
        settings.setValue(kKeyAudioInputDevice, setup.inputDeviceName);
        settings.setValue(kKeyAudioSampleRate, static_cast<qint64>(setup.sampleRate));
        settings.setValue(kKeyAudioBufferSize, setup.bufferSize);
    }
```

The full `onApply()` should now look like:

```cpp
void PreferencesDialog::onApply()
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeyClipDur, clipDurSpinBox->value());
    settings.setValue(kKeySnap, snapCheckBox->isChecked());
    settings.setValue(kKeySnapDiv, snapDivisionCombo->currentIndex());
    settings.setValue(kKeyMcpHost, mcpHostEdit->text());
    settings.setValue(kKeyMcpPort, mcpPortSpin->value());
    settings.setValue(kKeyMcpEnabled, mcpAutoStartCheck->isChecked());
    if (countInBarsSpin != nullptr)
        settings.setValue(kKeyCountInBars, countInBarsSpin->value());
    if (autoTempoMatchCheck != nullptr)
        settings.setValue(kKeyAutoTempoMatch, autoTempoMatchCheck->isChecked());
    settings.setValue(kKeyDefaultProjectDir, defaultProjectDirEdit->text());
    settings.setValue(kKeyDefaultAudioDir, defaultAudioDirEdit->text());
    settings.setValue(kKeyDefaultMidiDir, defaultMidiDirEdit->text());

    // Persist audio device settings
    if (audioEngine != nullptr)
    {
        auto& dm = audioEngine->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        settings.setValue(kKeyAudioDriver, deviceTypeCombo->currentText());
        settings.setValue(kKeyAudioOutputDevice, setup.outputDeviceName);
        settings.setValue(kKeyAudioInputDevice, setup.inputDeviceName);
        settings.setValue(kKeyAudioSampleRate, static_cast<qint64>(setup.sampleRate));
        settings.setValue(kKeyAudioBufferSize, setup.bufferSize);
    }

    emit preferencesApplied();
}
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/ui/PreferencesDialog.cpp
git commit -m "PreferencesDialog: save audio device settings to QSettings on apply"
```

---

## Task 3: Restore audio device settings on startup

**Files:**
- Modify: `src/engine/AudioEngine.cpp:103-115` (the audio device init section in `initialize()`)

- [ ] **Step 1: Add restore logic after default device init**

Replace the block from line 103 (`// Initialize default audio device`) through line 115 (`deviceManager.addAudioCallback(&processorPlayer);`) with:

```cpp
    // Initialize default audio device (2 in, 2 out) as fallback
    auto error = deviceManager.initialiseWithDefaultDevices(2, 2);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("AudioEngine::initialize Error: " + error);

    // Restore saved audio device preferences if available
    {
        QSettings s;
        QString savedDriver = s.value(PreferencesDialog::kKeyAudioDriver).toString();
        QString savedOutput = s.value(PreferencesDialog::kKeyAudioOutputDevice).toString();
        QString savedInput  = s.value(PreferencesDialog::kKeyAudioInputDevice).toString();
        int savedRate       = s.value(PreferencesDialog::kKeyAudioSampleRate, 0).toInt();
        int savedBuffer     = s.value(PreferencesDialog::kKeyAudioBufferSize, 0).toInt();

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
                juce::Logger::writeToLog("AudioEngine: saved device restore failed: " + err
                    + " — using defaults");
                deviceManager.initialiseWithDefaultDevices(2, 2);
            }
        }
    }

    // Connect processor to player (triggers prepareToPlay → RoutingManager rebuild)
    processorPlayer.setProcessor(mainProcessor.get());

    // Add player as audio callback
    deviceManager.addAudioCallback(&processorPlayer);
```

- [ ] **Step 2: Add the QSettings include at top of AudioEngine.cpp**

The file needs `#include <QSettings>` and `#include "PreferencesDialog.h"` (for the key constants). Check the existing includes at the top of the file and add if missing.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngine.cpp
git commit -m "AudioEngine: restore saved audio device settings on startup"
```

---

## Task 4: Build and run tests

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: clean compile with no errors

- [ ] **Step 2: Run all tests**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all tests pass

---

## Summary of Changes

| File | Lines | Change |
|------|-------|--------|
| `src/ui/PreferencesDialog.h` | +5 | Add `kKeyAudio*` constants |
| `src/ui/PreferencesDialog.cpp` | +10 | Save audio device settings in `onApply()` |
| `src/engine/AudioEngine.cpp` | +30 | Restore saved device settings with fallback |
