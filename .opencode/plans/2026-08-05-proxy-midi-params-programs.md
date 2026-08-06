# Fix: remaining proxy-boundary gaps — MIDI fidelity, plugin params, programs

Date: 2026-08-05
Status: planned
Follows: 2026-08-05-proxy-state-truncation-fix.md (same audit)

## Goals

A. MIDI crosses the isolation proxy faithfully: SysEx (any length ≤128 KB) and
   short messages of 1–3 bytes (today every message is mangled to exactly 3
   bytes — 2-byte messages like program change get a garbage third byte).
B. Plugin parameters work through the proxy: managed-parameter API
   (`getParameters()`) on the parent proxy, lock-free audio-thread set path
   (automation), MCP/UI get/set, and child→parent change notifications
   (plugin-editor knob moves reach parent listeners).
C. Programs/presets work through the proxy: count/names/current/set, so MCP
   `list_plugin_presets` / `load_plugin_preset` work on isolated slots
   (MCP-parity rule).

## Success Gates

- [ ] G1: Build clean; HDAW.exe + hdaw_plugin_host.exe rebuilt together.
- [ ] G2: Integration test: SysEx round-trip through a live `__midiecho__`
      child via `PluginProxySlot::processBlock` — byte-exact, incl. a SysEx
      >244 bytes (proves the sysex lane) and a 2-byte message (proves size
      fidelity).
- [ ] G3: Integration test: param bridge — `__stateecho__` gains 3 named
      managed params; proxy `getParameters()` reports 3 with correct names;
      `setValue` from message thread reaches child (read back); audio-thread
      staging path exercised; child-side param change notifies parent
      listeners (PARAM_CHANGED notify ring).
- [ ] G4: Integration test: program bridge — count/names/current/set
      round-trip on `__stateecho__` (2 named programs).
- [ ] G5: Full `hdaw_tests.exe` passes.
- [ ] G6: Audio thread stays allocation/lock-free except the documented
      rare-event exception (reconstructing a received SysEx MidiMessage).
- [ ] G7: AudioEngine.cpp:901-903 misleading comment corrected.

## Dependency Map / facts established

- `TrackFXSlot::applyAutomation()` (TrackFXSlot.h:173) runs on the AUDIO thread
  (Track.cpp:440) and calls `params[i]->setValue(v)` → proxy parameter objects
  MUST be lock-free on setValue/getValue.
- `PluginParamServiceImpl` (message thread, RPC/MCP) uses managed parameters:
  getParameters/getName/getValue/getText/getLabel/isAutomatable,
  beginChangeGesture/setValueNotifyingHost/endChangeGesture, addListener.
- `AudioEngine::getFxProgramList` → TrackFXSlot::getNumPrograms/getProgramName
  → pluginInstance (proxy today: hardcoded stubs).
- Child `PluginHost::run()` sends READY **before** loadPlugin(), but the
  controlLoop starts **after** loadPlugin() → any pipe request/response
  round-trip from the parent naturally blocks until the plugin is loaded.
  Param/program metadata fetch at proxy construction is therefore safe.
- `PARAM_CHANGED` message type exists but is unused (no child listener, no
  parent consumer).
- ShmHeader layout change → bump SHM_MAGIC.

## Design

### Task A — MIDI fidelity (SysEx lane + short-message size)

`src/proxy/ProxyCommon.h`:
- `MidiEvent`: `flags` byte (replaces `_pad`): bit 7 = SysEx reference;
  low bits = inline byte count (1–3) when bit 7 clear. Add `uint32_t sysexLen`
  member (valid when flags bit 7 set).
- `ShmHeader`: add `std::atomic<uint32_t> sysexInBusy{0}, sysexOutBusy{0}`.
- New shm regions: one 128 KB SysEx buffer per direction
  (`computeShmSize` += 2×128 KB; `ShmRegion` accessors `getSysexInBuffer()` /
  `getSysexOutBuffer()`). Bump SHM_MAGIC.
- Protocol: ONE in-flight SysEx per direction. Writer: if busy flag set →
  drop + HDAW_LOG (rate-limited); else memcpy bytes to buffer[0..len), store
  len in event, publish event, set busy=1. Reader: on SysEx event, copy bytes
  out, clear busy=0. Ordering guaranteed by existing ring write-pos
  release/acquire. SysEx >128 KB → drop + log.

`src/proxy/PluginProxySlot.cpp` processBlock:
- Write path: `msg.isSysEx()` → SysEx lane; else inline with actual
  `getDataSize()` (1–3) in flags.
- Read path: reconstruct — inline: `juce::MidiMessage(data, size, offset)`;
  SysEx: `juce::MidiMessage(buf, len)` (heap alloc — accepted rare exception,
  documented comment).

`src/proxy/host/PluginHost.cpp` audioLoop: mirror image (midiIn → plugin,
plugin out → midiOut) using the child-side SysEx buffer + JUCE
`MidiMessage(const void*, int, double)` reconstruction.

`src/proxy/host/PluginHost.cpp` test plugin:
- New `MidiEchoProcessor` (`__midiecho__`): audio passthrough + copies every
  incoming MidiMessage to the output buffer verbatim (processBlock). Wire into
  loadPluginByPath.

Tests (`tests/integration/proxy/isolation_integration_test.cpp`):
- `SysExRoundTripThroughProxy`: spawn `__midiecho__`, PluginProxySlot over the
  live shm/pipe, call processBlock with a patterned SysEx of ~2000 bytes and a
  2-byte program-change; poll processBlock until output MIDI contains both;
  assert byte-exact SysEx and exact 2-byte size.

### Task B — Parameter & program bridge

Protocol (`ProxyCommon.h`, append message types):
- Fix existing: `SET_PARAM` request = {uint32 index, float value} — child
  applies via its own managed param (`setValue`); `GET_PARAM` request =
  {uint32 index} → response float; `GET_PARAM_INFO` request = {uint32 index} →
  response {float defaultValue; uint8 automatable; uint32 nameLen; char
  name[..]} — chunk name with STATE_CHUNK if >~230 bytes; `GET_PARAM_COUNT`
  already implemented.
- New: `GET_PROGRAM_COUNT(+_RESULT)`, `GET_PROGRAM_NAME(+_RESULT)` (chunked
  like state), `SET_PROGRAM(+_RESULT)`, `GET_CURRENT_PROGRAM(+_RESULT)`.

Shm param bridge (`ProxyCommon.h` / `ShmRegion`):
- Parent→child set ring: 256 × packed `uint64` {index<<32 | float bits} +
  atomic write/read pos. Single writer: the parent AUDIO thread only.
- Child→parent notify ring: same layout + atomics. Single writer: child
  param-listener callback.

Child (`PluginHost`):
- AudioLoop drains parent→child set ring each block → managed
  `params[i]->setValue(v)` (bounds-checked).
- `AudioProcessorListener` on the hosted plugin:
  `audioProcessorParameterChanged` → write {index,value} to notify ring
  (lock-free; drop when full).

Parent (`PluginProxySlot` + new `ProxiedParameter` in PluginProxySlot.h/.cpp):
- At construction (message thread; blocks until child loaded — safe per facts
  above): GET_PARAM_COUNT, per-param GET_PARAM_INFO + GET_PARAM; create
  `ProxiedParameter` objects (juce::AudioProcessorParameter subclass: cached
  name/label/automatable/default; `getValue` reads parent-local
  `std::atomic<float>` cache; `setValue` writes cache + marks staging slot
  dirty; `getNumSteps` sensible default) and `addParameter` them.
- Staging: parent-local `dirty[]` + `staged[]` atomics. `processBlock` flushes
  dirty entries into the shm set ring (single writer preserved). Message-thread
  sets (MCP/UI) land next audio block (~≤1 block latency).
- `processBlock` also drains the notify ring → updates atomic cache + pushes
  into a parent-local bounded queue; `timerCallback` (change 5000 ms → 100 ms;
  state-save every 50th tick) drains the queue on the message thread and calls
  `sendValueChangedMessageToListeners` (JUCE listener contract = message
  thread). This makes `setParamChangeCallback` / plugin-editor knob moves work.
- Programs: override getNumPrograms (cached at construction),
  getCurrentProgram, setCurrentProgram, getProgramName (IPC, chunked names).
- Crashed child: all IPC paths early-out (existing `crashed`/pipe guards);
  params report cached values.
- After `migrateToNewSlot` (respawn): rings live in the new shm automatically
  (shmHandle swapped); nothing extra needed.

Misc:
- `AudioEngine.cpp:901-903` comment: correct — plugin params cross via the
  proxy shm param bridge, not "the SPSC bridge".

### Accepted limitations (document, don't block)

- Parameter gestures flatten to setValue across the proxy (plugin-side
  gesture recording not forwarded).
- `getText` on proxied params returns numeric default (no GET_PARAM_TEXT
  round-trip) — cosmetic.
- SysEx >128 KB and SysEx bursts beyond one-in-flight are dropped + logged.
- Param sets issued while the audio device is fully stopped propagate on
  resume.

## Pitfall gates

- Gate 1: param values restored implicitly (child state blob carries param
  values; bridge re-fetches at construction after respawn/rebuild).
- Gate 3: audio thread stays lock/allocation-free except the documented SysEx
  reconstruction; rings are SPSC with acquire/release; staging keeps the set
  ring single-writer.
- Gate 4: both binaries rebuilt together; SHM_MAGIC bumped.
- Gate 9: all indices bounds-checked against ring capacity / param count.

## Execution

Serialized (shared files): Task A first → review → Task B.

## Verification

```
cmake --build build --config Debug
build/Debug/hdaw_tests.exe --gtest_filter=PluginIsolation.*
build/Debug/hdaw_tests.exe
```
