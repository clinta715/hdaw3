cmake_minimum_required(VERSION 3.24)
project(HDAW VERSION 0.12.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
find_package(Qt6 REQUIRED COMPONENTS Widgets Network HttpServer WebSockets Test)
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
                $<TARGET_FILE_DIR:${target}>
        COMMENT "Running windeployqt for ${target}"
        VERBATIM
    )
endfunction()

# --- HDAW_lib: engine + model static library (no GUI / Qt Widgets) ---
# Contains all non-GUI sources so tests can link the engine+model
# without spinning up a QApplication. Future MCP sources will be added
# here in later tasks.
option(HDAW_PLUGIN_ISOLATION "Build with plugin process isolation support" OFF)
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
    src/engine/AudioEngineCommands_GhostPaint.cpp
    src/engine/AudioEngineCommands_Markers.cpp
    src/engine/AudioEngineCommands_Tracks.cpp
    src/engine/AudioEngineCommands_Modulation.cpp
    src/engine/AudioEngineCommands_Undo.cpp
    src/engine/AudioRecorder.cpp
    src/engine/CLAPPluginEditor.h
    src/engine/CLAPPluginEditor.cpp
    src/engine/CLAPPluginFormat.cpp
    src/engine/CLAPPluginInstance.cpp
    src/engine/ExportManager.cpp
    src/engine/MainAudioProcessor.cpp
    src/engine/PhraseGenerator.cpp
    src/engine/PluginManager.cpp
    src/engine/MidiInputManager.cpp
    src/engine/ProjectSerializer.cpp
    src/engine/RoutingManager.cpp
    src/engine/Track.cpp
    src/engine/TrackFXSlot.cpp
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
    src/model/ProjectModel.cpp
    src/mcp/McpJsonRpc.cpp
    src/mcp/McpSchema.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpExportTool.cpp
    src/mcp/McpTools.cpp
    src/mcp/McpTools_Project.cpp
    src/mcp/McpTools_Transport.cpp
    src/mcp/McpTools_Audio.cpp
    src/mcp/McpGuiInspectTools.cpp
    src/mcp/McpTransportLoopback.cpp
    src/mcp/McpTransportStdio.cpp
    src/mcp/McpTransportHttp.cpp
    src/proxy/ProxyPipe.cpp
    src/proxy/ProxySharedMemory.cpp
    src/ui/PluginEditorWindow.cpp
    src/engine/RegionClipboard.cpp
    src/frontend/FrontendRouter.cpp
    src/frontend/FrontendServer.cpp
    src/frontend/FrontendTreeWatcher.cpp
    src/frontend/UiHttpServer.cpp
)

if(HDAW_PLUGIN_ISOLATION)
    target_sources(HDAW_lib PRIVATE
        src/proxy/ProxyProcessManager.cpp
        src/proxy/PluginProxySlot.cpp
        src/proxy/ProxyEditor.cpp
    )
    target_compile_definitions(HDAW_lib PUBLIC HDAW_PLUGIN_ISOLATION=1)
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

# --- HDAW: main executable ---
# Default build (HDAW_GUI=OFF): engine + browser, no Qt Widgets dependency.
# With -DHDAW_GUI=ON: adds Qt desktop GUI mode (--gui flag).
option(HDAW_GUI "Build with Qt desktop GUI support (default: browser-only)" OFF)

add_executable(HDAW
    src/main.cpp
    src/ui/GuiInspector.cpp
    src/resources/resources.rc
    src/resources/resources.qrc
    src/resources/frontend.qrc
)

if(HDAW_GUI)
    target_sources(HDAW PRIVATE
        src/ui/MainWindow.h
        src/ui/MainWindow.cpp
        src/ui/MainWindowMenus.cpp
        src/ui/MainWindowConnections.cpp
        src/ui/VUMeter.h
        src/ui/VUMeter.cpp
        src/ui/TimelineView.h
        src/ui/TimelineView.cpp
        src/ui/TimelineScene.h
        src/ui/TimelineScene.cpp
        src/ui/TimelineToolbar.h
        src/ui/TimelineToolbar.cpp
        src/ui/TimelineInteraction.h
        src/ui/TimelineInteraction.cpp
        src/ui/TrackHeaderWidget.h
        src/ui/TrackHeaderWidget.cpp
        src/ui/ClipItem.h
        src/ui/ClipItem.cpp
        src/ui/AudioClipItem.h
        src/ui/AudioClipItem.cpp
        src/ui/MidiClipItem.h
        src/ui/MidiClipItem.cpp
        src/ui/TimeRuler.h
        src/ui/TimeRuler.cpp
        src/ui/PlayheadCursor.h
        src/ui/PlayheadCursor.cpp
        src/ui/LoopMarker.h
        src/ui/LoopMarker.cpp
        src/ui/MarkerItem.h
        src/ui/MarkerItem.cpp
        src/ui/MixerWidget.h
        src/ui/MixerWidget.cpp
        src/ui/MixerStripWidget.h
        src/ui/MixerStripWidget.cpp
        src/ui/PianoRollModel.h
        src/ui/PianoRollWidget.h
        src/ui/PianoRollWidget.cpp
        src/ui/PianoKeysWidget.h
        src/ui/PianoKeysWidget.cpp
        src/ui/NoteGridWidget.h
        src/ui/NoteGridWidget.cpp
        src/ui/VelocityLaneWidget.h
        src/ui/VelocityLaneWidget.cpp
        src/ui/CCLaneWidget.h
        src/ui/CCLaneWidget.cpp
        src/ui/PianoRollRuler.h
        src/ui/PianoRollRuler.cpp
        src/ui/StepEditorWidget.h
        src/ui/StepEditorWidget.cpp
        src/ui/StatusBar.h
        src/ui/StatusBar.cpp
        src/ui/StartupDialog.h
        src/ui/StartupDialog.cpp
        src/ui/AboutDialog.h
        src/ui/AboutDialog.cpp
        src/ui/PhraseGeneratorDialog.h
        src/ui/PhraseGeneratorDialog.cpp
        src/ui/FXChainWidget.h
        src/ui/FXChainWidget.cpp
        src/ui/FXSlotRow.h
        src/ui/FXSlotRow.cpp
        src/ui/AutomationLaneWidget.h
        src/ui/AutomationLaneWidget.cpp
        src/ui/ModulationWidget.h
        src/ui/ModulationWidget.cpp
        src/ui/AudioClipEditorWidget.h
        src/ui/AudioClipEditorWidget.cpp
        src/ui/GainEnvelopeEditor.h
        src/ui/GainEnvelopeEditor.cpp
        src/ui/AudioWaveformWidget.h
        src/ui/AudioWaveformWidget.cpp
        src/ui/Theme.h
        src/ui/ProjectPoolBrowser.h
        src/ui/ProjectPoolBrowser.cpp
        src/ui/ExportDialog.h
        src/ui/ExportDialog.cpp
        src/ui/PluginScannerDialog.h
        src/ui/PluginScannerDialog.cpp
        src/ui/ScanProgressDialog.h
        src/ui/ScanProgressDialog.cpp
        src/ui/BpmInputDialog.h
        src/ui/BpmInputDialog.cpp
        src/ui/PreferencesDialog.h
        src/ui/PreferencesDialog.cpp
    )
    target_compile_definitions(HDAW PRIVATE HDAW_GUI=1)
endif()

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

# Qt6::Widgets only needed for the GUI mode.
if(HDAW_GUI)
    target_link_libraries(HDAW PRIVATE Qt6::Widgets)
endif()

# Link-Time Optimization: allows inlining across .cpp boundaries (10-30% runtime speedup)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set_property(TARGET HDAW_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET HDAW PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# JUCE_PLUGINHOST_VST3 / JUCE_MODAL_LOOPS_PERMITTED are inherited from HDAW_lib (PUBLIC)

# Enable modern C++ features for MSVC; /MP enables parallel compilation
if(MSVC)
    target_compile_options(HDAW_lib PRIVATE /MP)
    target_compile_options(HDAW PRIVATE /W4 /permissive- /EHa /MP)
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
    src/ui/GuiInspector.cpp
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
    target_link_libraries(hdaw_plugin_host PRIVATE
        HDAW_lib
        juce::juce_audio_processors
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
hdaw_deploy_qt(hdaw_plugin_scanner)

# --- Tests (MCP pilot) ---
option(HDAW_BUILD_TESTS "Build HDAW unit/integration tests" ON)
if(HDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

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
