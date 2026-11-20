cmake_minimum_required(VERSION 3.22)
project(SLOOPStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# --- Pure C++ core + tests (no JUCE) -----------------------------------------
add_library(sloop_core INTERFACE)
target_include_directories(sloop_core INTERFACE Source/Core)

enable_testing()
add_executable(sloop_core_tests Tests/CoreTests.cpp)
target_link_libraries(sloop_core_tests PRIVATE sloop_core)
add_test(NAME core COMMAND sloop_core_tests)

# The sample slot port against SLOOP's own Python output (Tests/fixtures, made by mkref.py)
add_executable(sloop_sample_tests Tests/SampleSlotTests.cpp)
target_link_libraries(sloop_sample_tests PRIVATE sloop_core)
add_test(NAME samples COMMAND sloop_sample_tests ${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures)

# --- Plugin -------------------------------------------------------------------
option(SLOOP_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(SLOOP_BUILD_PLUGIN)
    add_subdirectory(external/JUCE)

    juce_add_plugin(SLOOPStudio
        COMPANY_NAME "HeyFresco"
        BUNDLE_ID "com.heyfresco.floopstudio"
        PLUGIN_MANUFACTURER_CODE Slps
        PLUGIN_CODE Sl01
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "Floop Studio"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        VST3_CATEGORIES Instrument Generator
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(SLOOPStudio PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/FM1Import.cpp
        Source/FM1Live.cpp
        Source/FM1Clock.cpp
        Source/SampleTools.cpp
        Source/SamplesPanel.cpp
        Source/MacMidiOut.cpp
        Source/SoundPanel.cpp
        Source/MidiMapPanel.cpp)

    target_compile_definitions(SLOOPStudio PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(SLOOPStudio
        PRIVATE
            sloop_core
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Loads the built VST3 like a host and checks its MIDI (Tests/HostCheck.cpp).
    juce_add_console_app(sloop_host_check PRODUCT_NAME "sloop_host_check")
    target_sources(sloop_host_check PRIVATE Tests/HostCheck.cpp)
    target_compile_definitions(sloop_host_check PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sloop_host_check PRIVATE sloop_core juce::juce_audio_processors juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags)

    # Renders the editor to PNG without a host (Tests/UiSnapshot.cpp).
    juce_add_console_app(sloop_ui_snapshot PRODUCT_NAME "sloop_ui_snapshot")
    target_sources(sloop_ui_snapshot PRIVATE Tests/UiSnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/FM1Import.cpp Source/FM1Live.cpp Source/FM1Clock.cpp Source/SampleTools.cpp Source/SamplesPanel.cpp Source/MacMidiOut.cpp Source/SoundPanel.cpp Source/MidiMapPanel.cpp)
    target_include_directories(sloop_ui_snapshot PRIVATE Source)
    target_compile_definitions(sloop_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sloop_ui_snapshot PRIVATE sloop_core juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)

    # Reads the pattern of a connected FM-1 and prints it (Tests/Fm1Probe.cpp): the import without FL.
    juce_add_console_app(sloop_fm1_probe PRODUCT_NAME "sloop_fm1_probe")
    target_sources(sloop_fm1_probe PRIVATE Tests/Fm1Probe.cpp Source/FM1Import.cpp Source/FM1Live.cpp Source/FM1Clock.cpp Source/SampleTools.cpp Source/MacMidiOut.cpp)
    target_include_directories(sloop_fm1_probe PRIVATE Source)
    target_compile_definitions(sloop_fm1_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sloop_fm1_probe PRIVATE sloop_core juce::juce_audio_devices juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()
