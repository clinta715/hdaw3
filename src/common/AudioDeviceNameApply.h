#pragma once
// O1 — validate the requested OUTPUT device name at the trust boundary, in ONE
// place, so the MCP tool (set_audio_output_device) and the frontend route
// (audio.setOutputDevice) cannot drift.
//
// The bug: both call sites used to set setup.outputDeviceName and call
// AudioDeviceManager::setAudioDeviceSetup without checking the returned
// error String — an unknown name "succeeded" (tool replied "ok"), dropped the
// open device, and persisted the dead name to QSettings. JUCE (v9.0.1)
// juce_AudioDeviceManager.cpp rejects an unknown name with "No such device: X"
// BEFORE touching the current device, so that refusal is non-destructive by
// itself. For the rarer OPEN failure on a KNOWN name (device unplugged/busy,
// driver gone — JUCE deleteCurrentDevice()s on that path) this helper
// ROLLS BACK: it snapshots the previous setup before opening and, if the open
// fails, re-applies it via setAudioDeviceSetup(previous, true) so the
// previously open device keeps running (P6-b). If the rollback also fails, the
// returned message names BOTH failures so the deviceless state is loud, never
// silent. On ANY non-empty return the caller must NOT persist QSettings.
//
// Documented spelling: an EMPTY name is the JUCE "no output / default" path
// (setAudioDeviceSetup closes the device when both names are empty) and is
// allowed through unchanged — closing is the intent there, so no rollback.
//
// Header-only (all inline), so no CMake source registration is needed.
//
// Return convention (matches the JUCE API this wraps): empty String = success,
// non-empty = refusal/failure message. On ANY non-empty return the caller must
// NOT write SettingsKeys::kKeyAudioOutputDevice.

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

namespace HDAW {

// The apply step as a callable so the rollback sequence is deterministically
// testable without hardware. The DEFAULT (a null callable) is the real
// `dm.setAudioDeviceSetup(setup, true)` — byte-identical production behaviour.
using DeviceSetupApplyFn = std::function<juce::String(
    juce::AudioDeviceManager&, const juce::AudioDeviceManager::AudioDeviceSetup&)>;

// Validated apply with rollback (the production step of applyOutputDeviceName).
// `name` must already be a KNOWN, non-empty device name (call the wrapper
// below — it does the validation). Snapshots the previous setup first; if the
// apply fails, re-applies the snapshot (P6-b rollback), and if that also
// fails, returns BOTH errors so the deviceless state is loud.
inline juce::String applyOutputDeviceValidated(juce::AudioDeviceManager& dm,
                                               const juce::String& name,
                                               const DeviceSetupApplyFn& apply = {})
{
    auto setup = dm.getAudioDeviceSetup();
    const auto previous = setup;
    setup.outputDeviceName = name;

    const auto run = [&dm](const DeviceSetupApplyFn& fn,
                           const juce::AudioDeviceManager::AudioDeviceSetup& s) {
        if (fn) return fn(dm, s);
        return dm.setAudioDeviceSetup(s, true);
    };

    const auto err = run(apply, setup);
    if (err.isNotEmpty())
    {
        // Open failed (JUCE has already deleted the current device on this
        // path) — restore the previous device so the session stays audible.
        const auto rollbackErr = run(apply, previous);
        juce::String msg = "could not open output device \"" + name + "\": " + err;
        if (rollbackErr.isNotEmpty())
            msg += "; rollback to previous device also failed: " + rollbackErr;
        return msg;
    }

    return {};
}

inline juce::String applyOutputDeviceName(juce::AudioDeviceManager& dm, const juce::String& name)
{
    if (name.isEmpty())
    {
        // Documented "close the output" spelling: same as before the fix —
        // clear only the output name on the current setup and let JUCE decide
        // (with both names empty JUCE closes the device; with an input still
        // named it re-opens input-only). Closing is the intent here, so no
        // rollback.
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = {};
        return dm.setAudioDeviceSetup(setup, true);
    }

    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return "no audio driver type is active — cannot resolve output device \"" + name + "\"";

    const auto available = type->getDeviceNames(false);
    if (! available.contains(name))
    {
        juce::StringArray quoted;
        for (const auto& d : available)
            quoted.add("\"" + d + "\"");

        return "unknown output device \"" + name + "\""
             + " — available: " + (quoted.isEmpty() ? juce::String("(none)") : quoted.joinIntoString(", "));
    }

    return applyOutputDeviceValidated(dm, name);
}

// Input twin of applyOutputDeviceValidated — identical sequence, but the name
// lands in setup.inputDeviceName. (The failure text says "input device".)
inline juce::String applyInputDeviceValidated(juce::AudioDeviceManager& dm,
                                              const juce::String& name,
                                              const DeviceSetupApplyFn& apply = {})
{
    auto setup = dm.getAudioDeviceSetup();
    const auto previous = setup;
    setup.inputDeviceName = name;

    const auto run = [&dm](const DeviceSetupApplyFn& fn,
                           const juce::AudioDeviceManager::AudioDeviceSetup& s) {
        if (fn) return fn(dm, s);
        return dm.setAudioDeviceSetup(s, true);
    };

    const auto err = run(apply, setup);
    if (err.isNotEmpty())
    {
        const auto rollbackErr = run(apply, previous);
        juce::String msg = "could not open input device \"" + name + "\": " + err;
        if (rollbackErr.isNotEmpty())
            msg += "; rollback to previous device also failed: " + rollbackErr;
        return msg;
    }

    return {};
}

inline juce::String applyInputDeviceName(juce::AudioDeviceManager& dm, const juce::String& name)
{
    if (name.isEmpty())
    {
        // Documented "no input" spelling: clear only the input name on the
        // current setup and let JUCE decide (an output still named re-opens
        // output-only). No rollback — clearing is the intent here.
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName = {};
        return dm.setAudioDeviceSetup(setup, true);
    }

    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return "no audio driver type is active — cannot resolve input device \"" + name + "\"";

    const auto available = type->getDeviceNames(true);
    if (! available.contains(name))
    {
        juce::StringArray quoted;
        for (const auto& d : available)
            quoted.add("\"" + d + "\"");

        return "unknown input device \"" + name + "\""
             + " — available: " + (quoted.isEmpty() ? juce::String("(none)") : quoted.joinIntoString(", "));
    }

    return applyInputDeviceValidated(dm, name);
}

} // namespace HDAW
