cmake_minimum_required(VERSION 3.24)
# CMP0141 (New in 3.25): honor CMAKE_MSVC_DEBUG_INFORMATION_FORMAT. The ninja
# build dir (build-ninja/, see build-fast.bat) configures with =Embedded
# (/Z7) so sccache + parallel cl processes never contend on a shared .pdb
# (C1041). VS generator keeps its default ProgramDatabase behavior.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
project(HDAW VERSION 0.39.3 LANGUAGES C CXX)

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

# --- Layered static libs (2026-09-29, plan Phase 2): monolithic lib split ---
# hdaw_common (src/common) <- hdaw_engine (src/engine + src/model) <- hdaw_surface
# (src/mcp + src/frontend); hdaw_proxy over common+engine. Same PUBLIC surface the
# monolith exposed (include dir, JUCE/Qt/clap links, defs, STL PCH, /arch:AVX2, IPO)
# so downstream targets only swap the old monolith for ${HDAW_LIBS}. Plan section 5.
# All non-GUI sources; tests link the engine+model layers without a QApplication.
option(HDAW_PLUGIN_ISOLATION "Build with plugin process isolation support" ON)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)

option(HDAW_ENABLE_AVX2 "Enable AVX2 SIMD instructions" ON)

set(HDAW_LIBS hdaw_common hdaw_engine hdaw_surface hdaw_proxy)

# --- hdaw_juce: JUCE module TUs compile ONCE here (plan Phase 3) ---
# The juce:: targets are INTERFACE carriers: their module sources and usage
# requirements are transplanted explicitly via TARGET_PROPERTY genexes.
# hdaw_juce compiles the sources PRIVATEly; the layer libs receive the
# identical include/def requirements PUBLICly (same genexes, same values -
# ODR-safe by construction) and never the sources, so no consumer re-compiles
# the module set (the per-consumer copies measured 212 juce-src steps of a
# 638-step full build). The juce:: targets are never linked anywhere.
add_library(hdaw_juce STATIC)
foreach(_mod juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices juce::juce_dsp juce::juce_core juce::juce_gui_basics)
    target_sources(hdaw_juce PRIVATE $<TARGET_PROPERTY:${_mod},INTERFACE_SOURCES>)
    target_include_directories(hdaw_juce PRIVATE $<TARGET_PROPERTY:${_mod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(hdaw_juce PRIVATE $<TARGET_PROPERTY:${_mod},INTERFACE_COMPILE_DEFINITIONS>)
    target_include_directories(hdaw_juce PUBLIC $<TARGET_PROPERTY:${_mod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(hdaw_juce PUBLIC $<TARGET_PROPERTY:${_mod},INTERFACE_COMPILE_DEFINITIONS>)
endforeach()
target_link_libraries(hdaw_juce PUBLIC clap_juce_extensions HDAW::SoundTouch HDAW::aubio)

add_library(hdaw_common STATIC
    src/common/MessagePumpThread.cpp
    src/common/HeadlessArgs.cpp
    src/common/DeviceParamMap.cpp
    src/common/PsyFmModMatrixView.cpp
    src/common/ParamOverrideLedger.cpp
    src/common/MatrixPresetService.cpp
    src/common/SongPlanView.cpp
    src/common/FxCaptureStatus.cpp
    src/common/TuningAnalysis.cpp
    src/common/MixReportJson.cpp
    src/common/ParamVerity.cpp
    src/common/ToneVerity.cpp
    src/common/MixVerdict.cpp
    src/common/BatchEnd.cpp
    src/common/ModulationCoverage.cpp
    src/common/NordBankLoader.cpp
    src/common/ProjectQuery.cpp
)

add_library(hdaw_engine STATIC
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
    src/engine/AudioEngineCommands_Song.cpp
    src/engine/AudioEngineCommands_Tracks.cpp
    src/engine/AudioEngineCommands_Session.cpp
    src/engine/SessionManager.cpp
    src/engine/AudioEngineCommands_Modulation.cpp
    src/engine/AudioEngineCommands_Undo.cpp
    src/engine/RaveService.cpp
    src/engine/RaveJobManager.cpp
    src/engine/RaveTrainingJobManager.cpp
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
    src/engine/MarkovRoles.h
    src/engine/MarkovArranger.h
    src/engine/MarkovArranger.cpp
    src/engine/CorpusArranger.cpp
    src/engine/PercussionEngine.h
    src/engine/PercussionEngine.cpp
    src/engine/HarmonyEngine.h
    src/engine/HarmonyEngine.cpp
    src/engine/TextureEngine.h
    src/engine/TextureEngine.cpp
    src/engine/SubtractiveSynthEngine.h
    src/engine/SubtractiveSynthEngine.cpp
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
    src/engine/VirusSysexImport.cpp
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
    src/engine/AudioImport.cpp
    src/engine/AudioPreviewPlayer.cpp
    src/engine/StretchRenderer.cpp
    src/engine/LoopAnalyzer.cpp
    src/engine/StretchCache.cpp
    src/engine/TransientDetector.cpp
    src/engine/FileLibraryManager.cpp
    src/engine/LibraryClusterer.cpp
    src/engine/ClusterPresetStore.cpp
    src/engine/RegionClipboard.cpp
    src/model/ProjectModel.cpp
)

add_library(hdaw_surface STATIC
    src/mcp/McpJsonRpc.cpp
    src/mcp/McpSchema.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpJobs.cpp
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
    src/mcp/McpTools_SongPlan.cpp
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
    src/mcp/McpTools_Rave.cpp
    src/mcp/McpTools_Matrix.cpp
    src/mcp/McpTools_Device.cpp
    src/mcp/McpTransportLoopback.cpp
    src/mcp/McpTransportStdio.cpp
    src/mcp/McpTransportHttp.cpp
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
    src/frontend/router/Router_Rave.cpp
    src/frontend/router/Router_Device.cpp
    src/frontend/router/Router_Matrix.cpp
    src/frontend/router/Router_Tuning.cpp
    src/frontend/router/Router_Modulation.cpp
    src/frontend/FrontendServer.cpp
    src/frontend/FrontendTreeWatcher.cpp
    src/frontend/TreeDeltaAccumulator.cpp
    src/frontend/UiHttpServer.cpp
)

add_library(hdaw_proxy STATIC
    src/proxy/ProxyPipe.cpp
    src/proxy/ProxySharedMemory.cpp
)

if(HDAW_PLUGIN_ISOLATION)
    target_sources(hdaw_proxy PRIVATE
        src/proxy/ProxyProcessManager.cpp
        src/proxy/PluginProxySlot.cpp
        src/proxy/ProxyEditor.cpp
        src/proxy/CrashDialog.cpp
    )
endif()

foreach(_lib hdaw_juce ${HDAW_LIBS})
    target_include_directories(${_lib} PUBLIC src)
    target_compile_definitions(${_lib} PUBLIC
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
    )
    target_precompile_headers(${_lib} PRIVATE
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
    if(MSVC)
        target_compile_options(${_lib} PRIVATE /MP /FS)
        if(_lib STREQUAL hdaw_juce)
            # Without /GL (hdaw_juce is IPO-off) the JUCE module objects are real
            # COFF objects and exceed the section limit (C1128 juce_gui_basics).
            target_compile_options(${_lib} PRIVATE /bigobj)
        endif()
        if(HDAW_ENABLE_AVX2)
            target_compile_options(${_lib} PRIVATE /arch:AVX2)
        endif()
        target_compile_options(${_lib} PRIVATE
            $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob2 /Oi /Oy /GT>
        )
    endif()
    if(ipo_supported)
        # hdaw_juce stays IPO-off: one LTO archive for the whole JUCE module set
        # made every downstream link run link-time codegen over all of JUCE
        # (engine-touch build 344.7 s -> 559.6/582.7 s, identical 13 steps, zero
        # JUCE compiles - deterministic across two runs). JUCE module code gains
        # little from cross-module LTO; the app libs keep theirs. A/B in plan 12.
        if(NOT _lib STREQUAL hdaw_juce)
            set_property(TARGET ${_lib} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
            set_property(TARGET ${_lib} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endif()
    if(HDAW_PLUGIN_ISOLATION)
        # PUBLIC on every lib, exactly as the old monolithic lib had it: PluginManager
        # and other non-proxy TUs compile #if HDAW_PLUGIN_ISOLATION blocks, so a
        # proxy-only def would silently compile isolation out of the engine layer.
        target_compile_definitions(${_lib} PUBLIC HDAW_PLUGIN_ISOLATION=1)
    endif()
endforeach()

# Layer order (plan section 5): common <- engine <- surface; proxy over common+engine.
target_link_libraries(hdaw_common PUBLIC hdaw_juce Qt6::Core Qt6::Network Qt6::HttpServer Qt6::WebSockets)
target_link_libraries(hdaw_engine PUBLIC hdaw_juce hdaw_common Qt6::Core Qt6::Network Qt6::HttpServer Qt6::WebSockets)
target_link_libraries(hdaw_surface PUBLIC hdaw_juce hdaw_common hdaw_engine Qt6::Core Qt6::Network Qt6::HttpServer Qt6::WebSockets)
target_link_libraries(hdaw_proxy PUBLIC hdaw_juce hdaw_common hdaw_engine Qt6::Core Qt6::Network Qt6::HttpServer Qt6::WebSockets)

if(HDAW_PLUGIN_ISOLATION)
    target_link_libraries(hdaw_proxy PUBLIC Qt6::Widgets)
endif()

# ── Unity Build ──────────────────────────────────────────────────────────────
# Unity builds are disabled for now because the codebase has files with
# conflicting definitions (msfa headers without include guards, MCP tools
# with aggregate initialization issues) that cause compilation errors when
# combined. This can be revisited after those issues are resolved.
# set_target_properties(hdaw_engine PROPERTIES
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
    ${HDAW_LIBS}
)



# Link-Time Optimization: allows inlining across .cpp boundaries (10-30% runtime
# speedup). Only for optimized configs — Debug keeps full debuggability.
if(ipo_supported)
    set_property(TARGET HDAW PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET HDAW PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# JUCE_PLUGINHOST_VST3 / JUCE_MODAL_LOOPS_PERMITTED are inherited from the layered libs (PUBLIC)

# Enable modern C++ features for MSVC; /MP enables parallel compilation
if(MSVC)
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
        target_compile_options(HDAW PRIVATE /arch:AVX2)
    endif()

    # Per-config optimization flags for Release and RelWithDebInfo.
    # Debug keeps /Od (no optimization) for full debuggability.
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
    ${HDAW_LIBS}
)
set_target_properties(HDAW_headless PROPERTIES
    OUTPUT_NAME "HDAW_headless"
)
# Deploy Qt DLLs next to the headless binary, same as the other Qt-linked
# exes (HDAW, hdaw_plugin_scanner, hdaw_tests). the layered libs pull in
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
        ${HDAW_LIBS}
    )
endif()

# --- hdaw_plugin_scanner: child process for isolated plugin scanning ---
add_executable(hdaw_plugin_scanner
    src/proxy/scanner/PluginScannerMain.cpp
)
target_include_directories(hdaw_plugin_scanner PRIVATE src)
target_link_libraries(hdaw_plugin_scanner PRIVATE
    ${HDAW_LIBS}
)
# NOTE: hdaw_plugin_scanner does NOT link Qt (JUCE-only exe — verified via
# dumpbin /DEPENDENTS: no Qt6*.dll in its import table). Do not call
# hdaw_deploy_qt() here: windeployqt fails the build on a non-Qt executable
# ("does not seem to be a Qt executable"), and there are no Qt DLLs to deploy.

# --- Engine-link PDB escape hatch (LNK1201) ---
# The multi-GB engine PDBs intermittently fail the link with LNK1201 ("error
# writing to program database") once debug info volume grows past a threshold,
# even with free disk and no locks. Set HDAW_LINK_NO_PDB=1 in the environment
# to link the engine executables with /DEBUG:NONE (no PDB; debugging switches
# to the tests binary, which keeps its PDB). Reversible and default-off.
if(DEFINED ENV{HDAW_LINK_NO_PDB})
    set_property(TARGET HDAW HDAW_headless APPEND PROPERTY LINK_FLAGS "/DEBUG:NONE")
    message(STATUS "HDAW_LINK_NO_PDB: linking engine executables without PDBs")
endif()

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

# --- WSL time-sync hook (Windows/WSL clock drift) ---------------------------
# WSL2 clocks drift from the Windows host; ninja/MSBuild then misjudge file
# mtimes through drvfs/9p (stale-`.obj` / stale-bundle traps - AGENTS.md
# lessons 15/21). Run scripts\time-sync.cmd before every default build via
# cmake/RunTimeSync.cmake. The hook is a fast no-op outside WSL / within the
# HDAW_TIME_SYNC_INTERVAL freshness window and NEVER fails a build. Disable
# with -DHDAW_TIME_SYNC_HOOK=OFF.
set(HDAW_TIME_SYNC_HOOK ON CACHE BOOL "Run WSL time-sync before builds (Windows/WSL clock-drift guard)")
if(HDAW_TIME_SYNC_HOOK AND WIN32)
    add_custom_target(hdaw_time_sync ALL
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTimeSync.cmake"
        COMMENT "WSL time sync (pre-build hook)"
        VERBATIM
    )
endif()

# --- Fast dev links (F-A build perf, 2026-09-18) ---
# Full links of hdaw_tests (59s) / HDAW_headless (61s) dominated incremental
# builds. /INCREMENTAL cuts relinks to seconds; it changes binary layout only,
# never codegen, so runtime behavior is identical. Toggle off for pristine
# release packaging (HDAW_INCREMENTAL_DEV_LINK=OFF).
option(HDAW_INCREMENTAL_DEV_LINK "Use /INCREMENTAL for the test exes + HDAW_headless dev links" ON)
if(HDAW_INCREMENTAL_DEV_LINK AND MSVC)
    # 2026-09-28 test-time split: the former single hdaw_tests target became four
    # per-seam exes (tests/CMakeLists.txt); each gets the same fast dev link.
    foreach(_test_exe IN ITEMS hdaw_tests_engine hdaw_tests_mcp hdaw_tests_frontend hdaw_tests_platform)
        set_property(TARGET ${_test_exe} APPEND_STRING PROPERTY LINK_FLAGS " /INCREMENTAL")
    endforeach()
    set_property(TARGET HDAW_headless APPEND_STRING PROPERTY LINK_FLAGS " /INCREMENTAL")
endif()
