cmake_minimum_required(VERSION 3.24)
project(HDAW VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6 Configuration
# Note: Set -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64 when running cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# JUCE Configuration
include(cmake/JUCEHelper.cmake)

add_executable(HDAW
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
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
    src/ui/FXChainWidget.h
    src/ui/FXChainWidget.cpp
    src/ui/FXSlotRow.h
    src/ui/FXSlotRow.cpp
    src/engine/AutomationManager.h
    src/engine/PluginManager.h
    src/engine/PluginManager.cpp
    src/engine/ProjectSerializer.h
    src/engine/ProjectSerializer.cpp
    src/engine/AudioRecorder.h
    src/engine/AudioRecorder.cpp
    src/engine/ExportManager.h
    src/engine/ExportManager.cpp
    src/ui/AutomationLaneWidget.h
    src/ui/AutomationLaneWidget.cpp
    src/ui/AudioClipEditorWidget.h
    src/ui/AudioClipEditorWidget.cpp
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
    src/ui/PreferencesDialog.h
    src/ui/PreferencesDialog.cpp
    src/engine/AudioEngine.h
    src/engine/AudioEngine.cpp
    src/engine/MainAudioProcessor.h
    src/engine/MainAudioProcessor.cpp
    src/engine/Track.h
    src/engine/Track.cpp
    src/engine/ProjectPool.h
    src/engine/SendProcessor.h
    src/engine/CLAPPluginFormat.h
    src/engine/CLAPPluginFormat.cpp
    src/engine/CLAPPluginInstance.h
    src/engine/CLAPPluginInstance.cpp
    src/engine/CLAPPluginEditor.h
    src/engine/CLAPPluginEditor.cpp
    src/engine/BusProcessorBase.h
    src/engine/MasterBusProcessor.h
    src/engine/GroupBusProcessor.h
    src/engine/FxBusProcessor.h
    src/engine/RoutingManager.h
    src/engine/RoutingManager.cpp
    src/engine/ClipSourceProcessor.h
    src/engine/MidiClipProcessor.h
    src/engine/TrackFXSlot.h
    src/model/ProjectModel.h
    src/model/ProjectModel.cpp
)

target_link_libraries(HDAW
    PRIVATE
    Qt6::Widgets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    clap_juce_extensions
)

# Enable VST3 plugin hosting
target_compile_definitions(HDAW PRIVATE 
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# Enable modern C++ features for MSVC
if(MSVC)
    target_compile_options(HDAW PRIVATE /W4 /permissive- /EHa)
endif()

set_target_properties(HDAW PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
