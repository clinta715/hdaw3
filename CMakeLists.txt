cmake_minimum_required(VERSION 3.24)
# CMP0141 (New in 3.25): honor CMAKE_MSVC_DEBUG_INFORMATION_FORMAT. The ninja
# build dir (build-ninja/, see build-fast.bat) configures with =Embedded
# (/Z7) so sccache + parallel cl processes never contend on a shared .pdb
# (C1041). VS generator keeps its default ProgramDatabase behavior.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
project(HDAW VERSION 0.28.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build Performance Optimizations ──────────────────────────────────────────
# 1. Skip FetchContent network checks when sources are already populated.
#    Prevents CMake from contacting GitHub on every configure, which adds
#    2-5 seconds per configure even when nothing changed.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# 2. Optional sccache compiler launcher (OPT-IN, default OFF).
#    2026-08-31: sccache auto-detect removed. With PCH enabled (below) nearly
#    every TU is non-cacheable to sccache (/Fp /Yc), so it added pure risk:
#    cache-dir permission failures, server 'address in use' flakiness, and
#    perturbed PDB contention timing (C1041) under parallel cl. Re-enable
#    explicitly with -DHDAW_USE_SCCACHE=ON if you run a no-PCH workflow.
option(HDAW_USE_SCCACHE "Wrap cl.exe with sccache (requires no-PCH workflow)" OFF)
if(HDAW_USE_SCCACHE)
    find_program(SCCACHE_EXECUTABLE sccache)
    if(SCCACHE_EXECUTABLE)
        message(STATUS "sccache launcher ON: ${SCCACHE_EXECUTABLE}")
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_EXECUTABLE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_EXECUTABLE})
    else()
        message(FATAL_ERROR "HDAW_USE_SCCACHE=ON but sccache not found in PATH")
    endif()
else()
    message(STATUS "sccache launcher OFF (default; -DHDAW_USE_SCCACHE=ON to enable)")
endif()

# 2b. Embedded debug info (/Z7) for ALL configs on Ninja. Parallel cl + shared
# .pdb (/Zi) under load hits C1041/C1090 'cannot open program database' —
# /Z7 embeds codeview into each .obj and removes the shared-PDB choke point.
# (CMP0141 honors this variable; VS generator default unchanged.)
if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

# Generate common/Version.h with the project version so C++ code can
# #include "common/Version.h" and use the HDAW_VERSION macro.
# CMake's project(HDAW VERSION ...) is the single source of truth.
configure_file(
    src/common/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/common/Version.h
    @ONLY
)
include_directories(${CMAKE_BINARY_DIR}/generated)

# Qt 6 Configuration
# Note: Set -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64 when running cmake
# Components: Core (implicit), Network/HttpServer/WebSockets (transport),
# Gui (QDesktopServices in main.cpp), Test (QSignalSpy in tests).
# Widgets is included for proxy::CrashDialog (plugin isolation only) and
# linked conditionally via Qt6::Widgets in the HDAW_PLUGIN_ISOLATION block.
find_package(Qt6 REQUIRED COMPONENTS Gui Network HttpServer WebSockets Test Widgets)
qt_standard_project_setup()

# JUCE Configuration
include(cmake/JUCEHelper.cmake)

# SoundTouch (LGPL-2.1) for pitch-preserving audio timestretch.
# Isolated behind HDAW's StretchRenderer interface (see AGENTS.md).
include(cmake/SoundTouchHelper.cmake)

# aubio (GPL-2.1) for BPM detection on audio import.
include(cmake/AubioHelper.cmake)

# Deploy Qt DLLs alongside an executable on Windows via windeployqt.
# Skipped silently on non-Windows or when windeployqt isn't found.
#
# A --debug/--release flag is passed per-config via a generator expression.
# Without it, windeployqt's auto-detection (which sniffs the binary for debug
# markers) mis-classifies RelWithDebInfo builds and silently skips some Qt
# DLLs the binary imports — notably Qt6Test.dll, which hdaw_tests needs
# (QSignalSpy). Forcing the variant makes the deploy complete and stable.
function(hdaw_deploy_qt target)
    if(NOT WIN32)
        return()
    endif()
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS ${Qt6_DIR}/../../../bin
              ${Qt6_DIR}/bin
    )
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(STATUS "windeployqt not found; skipping Qt deploy for ${target}")
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                $<IF:$<CONFIG:Debug>,--debug,--release>
                $<TARGET_FILE:${target}>
        COMMENT "Running windeployqt for ${target}"
        VERBATIM
    )
endfunction()

# --- HDAW_lib: engine + model static library (no GUI / Qt Widgets) ---
# Contains all non-GUI sources so tests can link the engine+model
# without spinning up a QApplication. Future MCP sources will be added
# here in later tasks.
option(HDAW_PLUGIN_ISOLATION "Build with plugin process isolation support" ON)
add_library(HDAW_lib STATIC
    src/engine/AudioEngine.cpp
    src/engine/AudioEngineCommands.cpp
    src/engine/AudioEngineCommands_Clips.cpp
    src/engine/AudioEngineCommands_Transport.cpp
    src/engine/AudioEngineCommands_Fx.cpp
    src/engine/AudioEngineCommands_Timestretch.cpp
    src/engine/AudioEngineCommands_Slicing.cpp
    src/engine/AudioEngineCommands_GainEnvelope.cpp
    src/engine/AudioEngineCommands_Midi.cpp
    src/engine/AudioEngineCommands_Automation.cpp
    src/engine/AudioEngineCommands_Envelope.cpp
    src/engine/AudioEngineCommands_GhostPaint.cpp
    src/engine/AudioEngineCommands_Composition.cpp
    src/engine/AudioEngineCommands_Markers.cpp
    src/engine/AudioEngineCommands_Arranger.cpp
    src/engine/AudioEngineCommands_Tracks.cpp
    src/engine/AudioEngineCommands_Session.cpp
    src/engine/SessionManager.cpp
    src/engine/AudioEngineCommands_Modulation.cpp
    src/engine/AudioEngineCommands_Undo.cpp
    src/engine/AudioRecorder.cpp
    src/engine/CLAPPluginEditor.h
    src/engine/CLAPPluginEditor.cpp
    src/engine/CLAPPluginFormat.cpp
    src/engine/CLAPPluginInstance.cpp
    src/engine/CLAPPresetDatabase.cpp
    src/engine/ExportManager.cpp
    src/engine/MainAudioProcessor.cpp
    src/engine/PhraseGenerator.cpp
    src/engine/PsytranceGenerator.cpp
    src/engine/PsytranceMarkovGenerator.cpp
    src/engine/PatternLibrary.cpp
    src/engine/ChainLibrary.cpp
    src/engine/EnvelopeGenerator.cpp
    src/engine/Generative.cpp
    src/engine/ArrangementGenerator.cpp
    src/engine/RhythmPatternGenerator.cpp
    src/engine/SamplerEngine.cpp
    src/engine/FmSynthEngine.cpp
    src/engine/GrowlBassEngine.cpp
    src/engine/PsyArpEngine.cpp
    src/engine/PsyFmOperator.cpp
    src/engine/PsyFmModMatrix.cpp
    src/engine/PsyFmPatches.cpp
    src/engine/PsyFmEngine.cpp
    src/engine/PsyFmAlgorithms.cpp
    src/engine/Dx7SysexImport.cpp
    src/engine/msfa/fm_core.cc
    src/engine/msfa/fm_op_kernel.cc
    src/engine/msfa/sin.cc
    src/engine/msfa/exp2.cc
    src/engine/msfa/env.cc
    src/engine/msfa/pitchenv.cc
    src/engine/msfa/freqlut.cc
    src/engine/msfa/lfo.cc
    src/engine/msfa/dx7note.cc
    src/engine/msfa/porta.cpp
        src/engine/PluginManager.cpp
        src/engine/CrashRecoveryManager.cpp
    src/engine/MidiInputManager.cpp
    src/engine/MidiImport.cpp
    src/engine/MidiAnalyzer.cpp
    src/engine/ProjectSerializer.cpp
    src/engine/ProjectBackup.cpp
    src/engine/RoutingManager.cpp
    src/engine/Track.cpp
    src/engine/TrackFXSlot.cpp
    src/engine/MidiFx.cpp
    src/engine/ReadModelImpl.cpp
    src/engine/PluginServiceImpl.cpp
    src/engine/PluginParamServiceImpl.cpp
    src/engine/MidiServiceImpl.cpp
    src/engine/AutomationClipboard.cpp
    src/engine/BpmDetector.cpp
    src/engine/BarSnap.cpp
    src/engine/AudioPreviewPlayer.cpp
src/engine/StretchRenderer.cpp
    src/engine/StretchCache.cpp
    src/engine/TransientDetector.cpp
    src/engine/FileLibraryManager.cpp
    src/engine/LibraryClusterer.cpp
    src/engine/ClusterPresetStore.cpp
    src/common/MessagePumpThread.cpp
    src/model/ProjectModel.cpp
    src/mcp/McpJsonRpc.cpp
    src/mcp/McpSchema.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpExportTool.cpp
    src/mcp/McpTools.cpp
    src/mcp/McpTools_Project.cpp
    src/mcp/McpTools_Read.cpp
    src/mcp/McpTools_Track.cpp
    src/mcp/McpTools_Clip.cpp
    src/mcp/McpTools_Note.cpp
    src/mcp/McpTools_Cc.cpp
    src/mcp/McpTools_Composition.cpp
    src/mcp/McpTools_CompositionTiming.cpp
    src/mcp/McpTools_CompositionGenerate.cpp
    src/mcp/McpTools_CompositionInstrument.cpp
    src/mcp/McpTools_CompositionPattern.cpp
    src/mcp/McpTools_Arranger.cpp
    src/mcp/McpTools_ProjectSaveLoad.cpp
    src/mcp/McpTools_Transport.cpp
    src/mcp/McpTools_Audio.cpp
    src/mcp/McpTools_AudioRead.cpp
    src/mcp/McpTools_Fx.cpp
    src/mcp/McpTools_FxSlot.cpp
    src/mcp/McpTools_FxPreset.cpp
    src/mcp/McpTools_FxChain.cpp
    src/mcp/McpTools_Sampler.cpp
    src/mcp/McpTools_FmSynth.cpp
    src/mcp/McpTools_PsyFm.cpp
    src/mcp/McpTools_MidiFx.cpp
    src/mcp/McpTools_Automation.cpp
    src/mcp/McpTools_Send.cpp
    src/mcp/McpTools_Envelope.cpp
    src/mcp/McpTools_Session.cpp
    src/mcp/McpTools_Modulation.cpp
    src/mcp/McpTools_Library.cpp
    src/mcp/McpTools_Settings.cpp
    src/mcp/McpTools_Engine.cpp
    src/mcp/McpTools_Tuning.cpp
    src/mcp/McpTransportLoopback.cpp
    src/mcp/McpTransportStdio.cpp
    src/mcp/McpTransportHttp.cpp
    src/proxy/ProxyPipe.cpp
    src/proxy/ProxySharedMemory.cpp
    src/engine/RegionClipboard.cpp
    src/frontend/FrontendRouter.cpp
    src/frontend/router/Router_Project.cpp
    src/frontend/router/Router_Session.cpp
    src/frontend/router/Router_Transport.cpp
    src/frontend/router/Router_AudioGraph.cpp
    src/frontend/router/Router_Read.cpp
    src/frontend/router/Router_Plugin.cpp
    src/frontend/router/Router_Midi.cpp
    src/frontend/router/Router_Audio.cpp
    src/frontend/router/Router_Export.cpp
    src/frontend/router/Router_Preview.cpp
    src/frontend/router/Router_Composition.cpp
    src/frontend/router/Router_Library.cpp
    src/frontend/router/Router_Sampler.cpp
    src/frontend/router/Router_Pool.cpp
    src/frontend/router/Router_PsyFm.cpp
    src/frontend/FrontendServer.cpp
    src/frontend/FrontendTreeWatcher.cpp
    src/frontend/TreeDeltaAccumulator.cpp
    src/frontend/UiHttpServer.cpp
)

if(HDAW_PLUGIN_ISOLATION)
    target_sources(HDAW_lib PRIVATE
        src/proxy/ProxyProcessManager.cpp
        src/proxy/PluginProxySlot.cpp
        src/proxy/ProxyEditor.cpp
        src/proxy/CrashDialog.cpp
    )
    target_compile_definitions(HDAW_lib PUBLIC HDAW_PLUGIN_ISOLATION=1)
    target_link_libraries(HDAW_lib PUBLIC Qt6::Widgets)
endif()

target_include_directories(HDAW_lib PUBLIC src)
target_link_libraries(HDAW_lib PUBLIC
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_core
    juce::juce_gui_basics
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::WebSockets
    clap_juce_extensions
    HDAW::SoundTouch
    HDAW::aubio
)

# Enable VST3 plugin hosting (needed by PluginManager.cpp -> juce::VST3PluginFormat)
target_compile_definitions(HDAW_lib PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# ── Precompiled Headers (PCH) ────────────────────────────────────────────────
# Pre-compile heavy headers to reduce per-TU compilation time by 30-50%.
# The PCH is shared across all .cpp files in HDAW_lib, so each file only
# compiles its own unique code instead of re-parsing JUCE/Qt/STL headers.
#
# NOTE: JUCE and Qt headers are excluded from PCH because they have complex
# macro dependencies and platform-specific configurations that can conflict
# with the PCH approach. The standard library headers provide the most benefit.
target_precompile_headers(HDAW_lib PRIVATE
    # Standard library (most frequently included)
    <string>
    <vector>
    <memory>
    <functional>
    <algorithm>
    <array>
    <span>
    <optional>
    <variant>
    <unordered_map>
    <map>
    <set>
    <mutex>
    <atomic>
    <thread>
    <chrono>
    <cassert>
    <cstring>
    <cmath>
)

# ── Unity Build ──────────────────────────────────────────────────────────────
# Unity builds are disabled for now because the codebase has files with
# conflicting definitions (msfa headers without include guards, MCP tools
# with aggregate initialization issues) that cause compilation errors when
# combined. This can be revisited after those issues are resolved.
# set_target_properties(HDAW_lib PROPERTIES
#     UNITY_BUILD ON
#     UNITY_BUILD_BATCH_SIZE 0
# )

# --- HDAW: main executable ---
add_executable(HDAW
    src/main.cpp
    src/resources/resources.rc
    src/resources/resources.qrc
    src/resources/frontend.qrc
)

target_link_libraries(HDAW
    PRIVATE
    Qt6::Gui
    Qt6::Network
    Qt6::HttpServer
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    clap_juce_extensions
    HDAW_lib
)



# Link-Time Optimization: allows inlining across .cpp boundaries (10-30% runtime
# speedup). Only for optimized configs — Debug keeps full debuggability.
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set_property(TARGET HDAW_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET HDAW_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET HDAW PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET HDAW PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# JUCE_PLUGINHOST_VST3 / JUCE_MODAL_LOOPS_PERMITTED are inherited from HDAW_lib (PUBLIC)

# Enable modern C++ features for MSVC; /MP enables parallel compilation
if(MSVC)
    target_compile_options(HDAW_lib PRIVATE /MP /FS)
    target_compile_options(HDAW PRIVATE /W4 /permissive- /EHa /MP)

    # Suppress C++20/C++23 std-library deprecation warnings (C4996
    # "…deprecated in C++20/C++23", enum-arithmetic C5054/C5055) emitted from
    # JUCE/Qt headers on the newest MSVC toolset (the project compiles as
    # C++20; these are stdlib deprecations, not actionable here). /wd4996 also
    # hides C-RT deprecations (strcpy etc.) — re-enable by removing this block
    # if those matter.
    add_compile_options(/wd4996 /wd5054 /wd5055)

    # AVX2 for DSP-heavy code (peak calculation, waveform rendering, audio
    # processing). Every desktop CPU from ~2013+ supports it. Disable with
    # -DHDAW_ENABLE_AVX2=OFF if targeting older hardware.
    option(HDAW_ENABLE_AVX2 "Enable AVX2 SIMD instructions" ON)
    if(HDAW_ENABLE_AVX2)
        target_compile_options(HDAW_lib PRIVATE /arch:AVX2)
        target_compile_options(HDAW PRIVATE /arch:AVX2)
    endif()

    # Per-config optimization flags for Release and RelWithDebInfo.
    # Debug keeps /Od (no optimization) for full debuggability.
    target_compile_options(HDAW_lib PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob2 /Oi /Oy /GT>
    )
    target_compile_options(HDAW PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob2 /Oi /Oy /GT>
    )
endif()

set_target_properties(HDAW PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

hdaw_deploy_qt(HDAW)

# Verify the frontend dist exists when building with UI support.
# If missing, the .qrc will fail to compile — this gives a clearer message.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/frontend/dist/index.html")
    message(WARNING
        "frontend/dist/index.html not found. "
        "Run 'cd frontend && npm run build' before building HDAW with --ui support. "
        "The HDAW target will fail to link without the frontend resources."
    )
endif()

# --- HDAW_headless: engine-only executable, no Qt Widgets / GUI ---
# Headless engine for running as a background service or with the
# HTML/Electron frontend. Links only Qt Core/Network/HttpServer/WebSockets
# and JUCE — no Qt6::Widgets, no QApplication, no windowing system.
# Usage:
#   HDAW_headless.exe --mcp-stdio         (MCP over stdin/stdout)
#   HDAW_headless.exe --headless          (WebSocket server, default)
#   HDAW_headless.exe --port=9000         (custom WebSocket port)
add_executable(HDAW_headless
    src/main_headless.cpp
)
target_link_libraries(HDAW_headless
    PRIVATE
    HDAW_lib
)
set_target_properties(HDAW_headless PROPERTIES
    OUTPUT_NAME "HDAW_headless"
)
# Deploy Qt DLLs next to the headless binary, same as the other Qt-linked
# exes (HDAW, hdaw_plugin_scanner, hdaw_tests). HDAW_lib pulls in
# Qt6::Core/Network/HttpServer/WebSockets, so without this a clean
# `cmake --build build --target HDAW_headless` produces a binary that fails
# to start with STATUS_DLL_NOT_FOUND. See AGENTS.md "Build" section.
hdaw_deploy_qt(HDAW_headless)
if(MSVC)
    target_compile_options(HDAW_headless PRIVATE /MP)
endif()

# --- hdaw_plugin_host: child process for isolated plugin hosting ---
if(HDAW_PLUGIN_ISOLATION)
    add_executable(hdaw_plugin_host
        src/proxy/host/main.cpp
        src/proxy/host/PluginHost.cpp
    )
    target_include_directories(hdaw_plugin_host PRIVATE src)
    if(MSVC)
        # /EHa: convert SEH faults raised inside plugin processBlock into C++
        # exceptions so the audioLoop try/catch (SIL CRASH containment) really
        # fires. Without it the crash lands in the process-wide handler
        # (Rip+1 resume loop / WER termination) instead of being swallowed.
        target_compile_options(hdaw_plugin_host PRIVATE /EHa)
    endif()
    target_link_libraries(hdaw_plugin_host PRIVATE
        HDAW_lib
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_events
        juce::juce_core
    )
endif()

# --- hdaw_plugin_scanner: child process for isolated plugin scanning ---
add_executable(hdaw_plugin_scanner
    src/proxy/scanner/PluginScannerMain.cpp
)
target_include_directories(hdaw_plugin_scanner PRIVATE src)
target_link_libraries(hdaw_plugin_scanner PRIVATE
    HDAW_lib
    juce::juce_audio_processors
    juce::juce_core
)
# NOTE: hdaw_plugin_scanner does NOT link Qt (JUCE-only exe — verified via
# dumpbin /DEPENDENTS: no Qt6*.dll in its import table). Do not call
# hdaw_deploy_qt() here: windeployqt fails the build on a non-Qt executable
# ("does not seem to be a Qt executable"), and there are no Qt DLLs to deploy.

# --- Tests (MCP pilot) ---
option(HDAW_BUILD_TESTS "Build HDAW unit/integration tests" ON)
if(HDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Test VST3 plugin for plugin host isolation testing ---
add_subdirectory(tests/test-plugin)

# --- Version sync check ---
# Verify that CMakeLists.txt project version matches frontend/package.json.
# Run with: cmake --build build --target check_version
add_custom_target(check_version ALL
    COMMAND ${CMAKE_COMMAND} -E echo "--- Version Sync Check ---"
    COMMAND ${CMAKE_COMMAND} -E echo "CMakeLists.txt version: ${PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E echo "Checking frontend/package.json..."
    COMMAND ${CMAKE_COMMAND} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckVersionSync.cmake"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Verifying version numbers are in sync across all build systems"
)
