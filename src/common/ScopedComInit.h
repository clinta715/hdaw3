#pragma once

// Process-wide COM initialiser for HDAW's Windows entry points.
//
// JUCE 8's WASAPI audio device type (juce_WASAPI_windows.cpp) never calls
// CoInitialize itself: the wasapi ComSmartPtr::CoCreateInstance path jasserts
// against CO_E_NOTINITIALIZED and its comment says the caller thread must have
// CoInitialize'd. With COM uninitialised, the WASAPI device *scan* returns an
// empty endpoint list (cached by hasScanned=true in juce_WASAPI_windows.cpp),
// AudioDeviceManager::initialiseWithDefaultDevices silently falls through to
// DirectSound (which needs no COM), and the user gets ~58 ms emulated-latency
// audio with jittery callbacks -> choppy/stuttering output. The saved
// "Windows Audio (Low Latency Mode)" preference restores onto DirectSound and
// stays there.
//
// Host-app responsibility: JUCE provides no scoped guard in 8.0.0 (verified by
// grep across juce_core/juce_audio_devices). HDAW runs a QCoreApplication (no
// QApplication) since dd76505 "refactor: remove Qt GUI", so Qt no longer
// OleInitialize's COM on the main thread the way the Qt Widgets app used to.
//
// Construct this as the OUTERMOST RAII guard on the main thread, before
// AudioEngine / ScopedJuceInitialiser_GUI, so the AudioDeviceManager lifecycle
// AND the audio.* RPCs (QWebSocketServer dispatches on the main Qt event
// loop) both run on a COM-initialised thread. Multi-threaded apartment is used
// so the main thread does not have to pump COM apartment messages; Qt's
// app.exec() remains the JUCE/GUI message loop.
//
// If a future dependency OleInitialize's the main thread as STA,
// CoInitializeEx returns RPC_E_CHANGED_MODE: COM is already available, so we
// skip CoUninitialize and let that other owner manage the lifetime.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

namespace HDAW
{

class ScopedComInit
{
public:
    ScopedComInit()
    {
#ifdef _WIN32
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ok_ = (hr == S_OK || hr == S_FALSE);
        // RPC_E_CHANGED_MODE means COM is already up (another owner) — no
        // action; we keep COM available without owning the lifetime.
#endif
    }

    ~ScopedComInit()
    {
#ifdef _WIN32
        if (ok_)
            ::CoUninitialize();
#endif
    }

    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

    bool isOk() const noexcept { return ok_; }

private:
    bool ok_ = false;
};

} // namespace HDAW
