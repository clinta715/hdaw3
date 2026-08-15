# Plugin Process Isolation — Design Spec

> **Date:** 2026-06-30
> **Status:** Approved
> **Goal:** Crash recovery — plugin crashes don't take down the DAW

## 1. Architecture

Three layers:

**DAW side (in HDAW process):**
- `PluginProxySlot` — drop-in `juce::AudioPluginInstance` replacement. Serializes every call into IPC messages sent to the child process.
- `ProxyProcessManager` — singleton that spawns/monitors/kills child processes. One child per plugin slot.

**Child side (`hdaw_plugin_host.exe`):**
- `PluginHost` — receives IPC messages, loads the actual plugin DLL (CLAP or VST3) via existing `CLAPPluginFormat` / JUCE `VST3PluginFormat`, dispatches calls.
- Three threads: audio (ring buffer I/O), control (pipe listener), GUI (plugin editor window).

**IPC transport:**
- Control channel: named pipe (`\\.\pipe\hdaw_proxy_{slotId}`), fixed-size 256-byte binary messages.
- Audio channel: shared memory (`hdaw_audio_{slotId}`), two SPSC lock-free ring buffers (input + output) plus MIDI rings.

```
DAW Audio Thread                Child Audio Thread
┌──────────────┐               ┌──────────────┐
│ write input  │──ring buf───> │ read input   │
│              │               │ plugin.process│
│ read output  │<──ring buf── │ write output │
└──────────────┘               └──────────────┘

DAW Main Thread                Child Control Thread
┌──────────────┐               ┌──────────────┐
│ pipe request │──pipe──────> │ dispatch     │
│ pipe response│<──pipe────── │ reply        │
└──────────────┘               └──────────────┘
```

## 2. IPC Protocol

Fixed-size binary messages, no parsing, no heap allocation.

**DAW → Child:**
```cpp
struct ProxyMessage {
    uint32_t type;       // MessageType enum
    uint32_t slotId;     // plugin instance id
    uint32_t dataSize;   // bytes of payload
    uint8_t  data[248];  // inline payload (total 256 bytes)
};
```

**Child → DAW:**
```cpp
struct ProxyResponse {
    uint32_t type;       // matches request type
    uint32_t result;     // 0 = error, 1 = success
    uint32_t dataSize;   // bytes of response payload
    uint8_t  data[248];  // inline payload
};
```

**Message types:**
- `PREPARE` — sampleRate, blockSize → success/fail
- `PROCESS_BLOCK` — wake-up signal; audio flows via ring buffer
- `SET_STATE` / `GET_STATE` — plugin state blob transfer
- `SET_PARAM` / `GET_PARAM` — parameter get/set
- `GET_PARAM_COUNT` / `GET_PARAM_INFO` — parameter enumeration
- `SHOW_EDITOR` / `CLOSE_EDITOR` — GUI lifecycle
- `SHUTDOWN` — graceful exit

## 3. Shared Memory Layout

```
Header (64 bytes):
  magic, numChannels, blockSize, sampleRate
  inputWritePos (atomic), inputReadPos (atomic)
  outputWritePos (atomic), outputReadPos (atomic)
  childAlive (atomic), dawAlive (atomic)

Input ring buffer:  blockSize * numChannels * 4 bytes
Output ring buffer: blockSize * numChannels * 4 bytes
MIDI input ring:    256 events * 16 bytes = 4KB
MIDI output ring:   256 events * 16 bytes = 4KB
```

For stereo 512-sample blocks: ~16KB total. Negligible.

**Ring buffer:** SPSC, lock-free, power-of-two sized. `memory_order_acquire`/`release` on atomic positions. Spin-wait on audio thread (< 100ns). No syscalls in the hot path.

## 4. Plugin Lifecycle

**Startup:**
1. DAW calls `ProxyProcessManager::spawnPluginHost(pluginPath, slotId)`
2. Manager creates shared memory + named pipe
3. Launches `hdaw_plugin_host.exe --slot={slotId} --pipe={pipeName} --shm={shmName}`
4. Child connects, maps shared memory, sends `READY`
5. DAW sends `PREPARE` → child loads plugin, calls `prepareToPlay`
6. Audio ring buffers become live

**Crash detection:**
- Pipe `ReadFile`/`WriteFile` returns `ERROR_BROKEN_PIPE`
- `WaitForSingleObject(childHandle)` returns `WAIT_OBJECT_0`
- `childAlive` atomic stops updating (500ms watchdog timeout)

**Recovery:**
1. Slot enters "crasted" state — `processBlock` returns silence
2. UI shows "Plugin crashed — Restart?" dialog
3. User clicks Restart → manager kills zombie, re-spawns, sends last state via `SET_STATE`
4. Plugin resumes from last auto-save point (≤5 seconds loss)

**State preservation:**
- Auto-save to temp file every 5 seconds
- Project save writes to separate snapshot
- On crash, DAW sends most recent snapshot to new child

**Graceful shutdown:**
- DAW sends `SHUTDOWN` → child deactivates plugin, exits
- DAW waits 2s, then `TerminateProcess` if needed

## 5. GUI Handling

- Plugin GUI lives entirely in the child process
- Child creates native Win32 window, embeds JUCE editor via `addToDesktop`
- DAW shows a proxy card in FX chain: plugin name, bypass, "Open Editor" button
- Clicking "Open Editor" → child opens floating window
- Closing floating window → child sends `EDITOR_CLOSED` back to DAW
- Parameter changes from plugin GUI relayed to DAW via `PARAM_CHANGED` messages

Future: remote rendering (D3D shared surface) for dockable plugin GUIs.

## 6. Error Handling

| Scenario | Detection | Response |
|----------|-----------|----------|
| DLL fails to load | Child sends `PREPARE_FAIL` | Error dialog, slot stays empty |
| Crash during process | Pipe broken + `childAlive` stops | Crash dialog, auto-restart |
| Crash during state save | Pipe broken mid-transfer | Use last auto-saved state |
| Child won't start | `CreateProcess` fails | Error dialog, slot stays empty |
| Child hangs | `childAlive` timeout 2s | Kill + treat as crash |
| DAW quits | Send `SHUTDOWN`, wait 2s, terminate | Clean exit |

Multiple plugins: each has its own child process. One crash does not affect others.

## 7. File Layout

```
src/proxy/
├── ProxyCommon.h              # Shared types
├── ProxyProcessManager.h/cpp  # DAW-side process management
├── PluginProxySlot.h/cpp      # DAW-side AudioPluginInstance wrapper
├── ProxyEditor.h/cpp          # DAW-side proxy editor UI
├── ProxySharedMemory.h/cpp    # DAW-side shared memory + ring buffers

src/proxy/host/
├── main.cpp                   # Child entry point
├── PluginHost.h/cpp           # Child-side plugin loader + dispatcher
├── HostAudioThread.h/cpp      # Child-side audio processing
├── HostControlThread.h/cpp    # Child-side pipe listener
├── HostGuiThread.h/cpp        # Child-side plugin editor window
```

## 8. Build Integration

- `HDAW_lib` gets `src/proxy/*.cpp` (except host/)
- New target `hdaw_plugin_host` gets `src/proxy/host/*.cpp`, links `HDAW_lib`
- `hdaw_deploy_qt(hdaw_plugin_host)`
- In-process path stays default; isolation is opt-in per plugin

## 9. Testing

- Unit tests: ring buffer SPSC correctness, overflow/underflow, message serialization
- Integration tests: spawn child, load test plugin, audio through, verify output
- Crash test: kill child mid-process, verify DAW detects and recovers
