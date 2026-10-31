cmake_minimum_required(VERSION 3.22)
project(MoodMKIIStudy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Windows: runtime estático (o .vst3 não depende do VC++ Redistributable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE (baixado no primeiro configure; precisa de internet e git) ---
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MoodMKIIStudy
    COMPANY_NAME                 "Study"
    PLUGIN_MANUFACTURER_CODE     Stdy
    PLUGIN_CODE                  Mdm2
    FORMATS                      VST3 Standalone
    PRODUCT_NAME                 "MOOD MKII Study"
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Fx Reverb Delay)

target_sources(MoodMKIIStudy PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(MoodMKIIStudy PRIVATE Source)

target_compile_definitions(MoodMKIIStudy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MoodMKIIStudy
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- (opcional) harness de testes do núcleo DSP, sem JUCE ---
option(MOOD_BUILD_DSP_TESTS "Compila Tests/dsp_test.cpp (núcleo DSP puro)" OFF)
if(MOOD_BUILD_DSP_TESTS)
    add_executable(mood_dsp_test Tests/dsp_test.cpp)
    target_compile_features(mood_dsp_test PRIVATE cxx_std_17)
endif()
