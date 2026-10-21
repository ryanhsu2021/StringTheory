cmake_minimum_required(VERSION 3.22)

project(StringTheory VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.4 (directory junction -> DotSequencer/JUCE, no duplicate copy)
add_subdirectory(JUCE)

juce_add_plugin(StringTheory
    COMPANY_NAME              "Ryan Hsu"
    IS_SYNTH                  TRUE           # built-in string synth + MIDI out
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE           # per-voice channel MIDI out to the host
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    PLUGIN_MANUFACTURER_CODE  Ryhs
    PLUGIN_CODE               Stth
    BUNDLE_ID                 com.ryanhsu.stringtheory
    FORMATS                   VST3 Standalone   # Standalone = EXE build
    PRODUCT_NAME              "String Theory"
)

target_compile_definitions(StringTheory PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Subpixel (ClearType) text on Windows. The UI is an opaque paper plate,
    # so fringing cannot occur; small letterspaced caps read much sharper.
    # Requires the JUCE_USE_CLEARTYPE_TEXT hook in juce_DirectX_windows.h.
    JUCE_USE_CLEARTYPE_TEXT=1
)

if (MSVC)
    target_compile_options(StringTheory PRIVATE /utf-8)
endif()

target_sources(StringTheory PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(StringTheory PRIVATE Source)

target_link_libraries(StringTheory
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- offline verification (off by default, never part of the plugin) --------
#   cmake -B build -DST_BUILD_TESTS=ON
#   cmake --build build --config Release --target StringAudioCheck
option(ST_BUILD_TESTS "Build the offline audio verification tool" OFF)
if (ST_BUILD_TESTS)
    juce_add_console_app(StringAudioCheck PRODUCT_NAME "StringAudioCheck")
    target_sources(StringAudioCheck PRIVATE
        tools/audio_check.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(StringAudioCheck PRIVATE Source)
    target_compile_definitions(StringAudioCheck PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )
    if (MSVC)
        target_compile_options(StringAudioCheck PRIVATE /utf-8)
    endif()
    target_link_libraries(StringAudioCheck PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
