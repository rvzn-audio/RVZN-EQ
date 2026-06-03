cmake_minimum_required(VERSION 3.22)

project(RVZN_V1_EQUALIZER VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(RVZN_V1_EQUALIZER
    PRODUCT_NAME              "RVZN V1 EQUALIZER"
    COMPANY_NAME              "RVZN"
    BUNDLE_ID                 "com.rvzn.eq"
    PLUGIN_MANUFACTURER_CODE  Manu
    PLUGIN_CODE               Pfon
    FORMATS                   VST3 AU
    VST3_CATEGORIES           Fx EQ
    AU_MAIN_TYPE              kAudioUnitType_Effect
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

juce_generate_juce_header(RVZN_V1_EQUALIZER)

target_sources(RVZN_V1_EQUALIZER PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(RVZN_V1_EQUALIZER PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(RVZN_V1_EQUALIZER
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_animation
        juce::juce_gui_extra
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_include_directories(RVZN_V1_EQUALIZER PRIVATE Source)
