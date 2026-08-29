cmake_minimum_required(VERSION 3.22)
project(Ambience VERSION 1.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(C:/JUCE ./JUCE)

juce_add_plugin(Ambience
    COMPANY_NAME            "OTODESK"
    PLUGIN_MANUFACTURER_CODE "Otdk"
    PLUGIN_CODE             "Amb1"
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Ambience1.2.1"
    PLUGIN_DESCRIPTION      "16-channel FDN Algorithmic Reverb"
    VST3_CATEGORIES         "Fx|Reverb"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST      FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION                 "1.2.1"
)

target_sources(Ambience PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PluginParameters.h
    Source/PluginParameters.cpp
    Source/PresetManager.h
    Source/PresetManager.cpp
    Source/GUI/AmbienceUI.h
    Source/GUI/AmbienceUI.cpp
    Source/AlgorithmPresets.h
    Source/DSP/DSPConstants.h
    Source/DSP/DelayMemory.h
    Source/DSP/BiquadFilters.h
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.h
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/UniversalEngine.h
    Source/DSP/UniversalEngine.cpp
    Source/DSP/AcousticMetrics.h
    Source/DSP/AcousticMetrics.cpp
    Source/GUI/DecayCurveViz.h
    Source/GUI/DecayCurveViz.cpp
    Source/DSP/Saturator.h
    Source/DSP/OutputLimiter.h
    Source/DSP/OutputEQ.h
)

target_include_directories(Ambience PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)

target_link_libraries(Ambience
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(Ambience PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    AMBIENCE_VERSION="1.2.1B002"
)

if(MSVC)
    target_compile_options(Ambience PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()

juce_generate_juce_header(Ambience)

# ============================================================================
# テストターゲット: NoiseBenchmarkRunner
# ============================================================================
juce_add_console_app(NoiseBenchmarkRunner
    PRODUCT_NAME "NoiseBenchmarkRunner"
)
target_sources(NoiseBenchmarkRunner PRIVATE
    TB/NoiseBenchmarkRunner.cpp
    TB/UniversalEngine_v121.cpp
    Source/DSP/UniversalEngine.cpp
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/AcousticMetrics.cpp
)
target_include_directories(NoiseBenchmarkRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/TB
)
target_link_libraries(NoiseBenchmarkRunner PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
)
juce_generate_juce_header(NoiseBenchmarkRunner)
if(MSVC)
    target_compile_options(NoiseBenchmarkRunner PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()

# ============================================================================
# テストターゲット: CriticalBenchmarkRunner (4大客観指標 55 Items)
# ============================================================================
juce_add_console_app(CriticalBenchmarkRunner
    PRODUCT_NAME "CriticalBenchmarkRunner"
)
target_sources(CriticalBenchmarkRunner PRIVATE
    TB/CriticalBenchmarkRunner.cpp
    TB/UniversalEngine_v121b008.cpp
    Source/DSP/UniversalEngine.cpp
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/AcousticMetrics.cpp
)

juce_add_console_app(ModDiffusionBenchmarkRunner
    PRODUCT_NAME "ModDiffusionBenchmarkRunner"
)
target_sources(ModDiffusionBenchmarkRunner PRIVATE
    TB/ModDiffusionBenchmarkRunner.cpp
    TB/UniversalEngine_v121b009.cpp
    Source/DSP/UniversalEngine.cpp
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/AcousticMetrics.cpp
)
target_include_directories(CriticalBenchmarkRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/TB
)
target_link_libraries(CriticalBenchmarkRunner PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
)
juce_generate_juce_header(CriticalBenchmarkRunner)

target_include_directories(ModDiffusionBenchmarkRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/TB
)
target_link_libraries(ModDiffusionBenchmarkRunner PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
)
juce_generate_juce_header(ModDiffusionBenchmarkRunner)

juce_add_console_app(DetailedAcousticBench
    PRODUCT_NAME "DetailedAcousticBench"
)
target_sources(DetailedAcousticBench PRIVATE
    TB/DetailedAcousticBench.cpp
    TB/UniversalEngine_v121b010.cpp
    Source/DSP/UniversalEngine.cpp
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/AcousticMetrics.cpp
)
target_include_directories(DetailedAcousticBench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/TB
)
target_link_libraries(DetailedAcousticBench PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
)
juce_generate_juce_header(DetailedAcousticBench)

juce_add_console_app(StressTestSaturation
    PRODUCT_NAME "StressTestSaturation"
)
target_sources(StressTestSaturation PRIVATE
    TB/StressTestSaturation.cpp
    TB/UniversalEngine_v121b010.cpp
    Source/DSP/UniversalEngine.cpp
    Source/DSP/BiquadFilters.cpp
    Source/DSP/MagnitudeResponseFitter.cpp
    Source/DSP/AcousticMetrics.cpp
)
target_include_directories(StressTestSaturation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
    ${CMAKE_CURRENT_SOURCE_DIR}/TB
)
target_link_libraries(StressTestSaturation PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
)
juce_generate_juce_header(StressTestSaturation)

if(MSVC)
    target_compile_options(CriticalBenchmarkRunner PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
    target_compile_options(ModDiffusionBenchmarkRunner PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
    target_compile_options(DetailedAcousticBench PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
    target_compile_options(StressTestSaturation PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()

# ★ テストターゲットを通常ビルドから除外 (ビルドの超高速化)
set_target_properties(CriticalBenchmarkRunner PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(ModDiffusionBenchmarkRunner PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(DetailedAcousticBench PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(StressTestSaturation PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(NoiseBenchmarkRunner PROPERTIES EXCLUDE_FROM_ALL TRUE)


