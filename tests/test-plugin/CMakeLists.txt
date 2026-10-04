juce_add_plugin(HDAWTestPlugin
    COMPANY_NAME "HDAW"
    PLUGIN_MANUFACTURER_CODE "Hdaw"
    PLUGIN_CODE "Ptst"
    FORMATS VST3
    PRODUCT_NAME "PassthroughTest"
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    IS_SYNTH FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(HDAWTestPlugin PRIVATE
    PassthroughPlugin.h
    Plugin.cpp
)

target_include_directories(HDAWTestPlugin PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(HDAWTestPlugin PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Match the layered-libs global defines (root CMakeLists.txt): without
    # JUCE_WEB_BROWSER=0 the Linux juce_gui_extra module TU pulls webkit2gtk
    # headers, and without JUCE_USE_CURL=0 juce_core pulls curl headers.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(HDAWTestPlugin PRIVATE
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_gui_basics
)
