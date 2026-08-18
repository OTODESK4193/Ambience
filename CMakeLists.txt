cmake_minimum_required(VERSION 3.22)
project(Ambience VERSION 1.1.0 LANGUAGES C CXX)
# ============================================================================
# C++20
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ============================================================================
# JUCE
# ============================================================================
add_subdirectory(C:/JUCE ${CMAKE_BINARY_DIR}/JUCE)
# ============================================================================
# 繝励Λ繧ｰ繧､繝ｳ螳夂ｾｩ
# ============================================================================
juce_add_plugin(Ambience
    COMPANY_NAME            "OTODESK"
    PLUGIN_MANUFACTURER_CODE "Otdk"
    PLUGIN_CODE             "Ambi"
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Ambience1.1"
    PLUGIN_DESCRIPTION      "16-channel FDN Algorithmic Reverb"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST      FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION                 "1.1.0"
)
# ============================================================================
# 繧ｽ繝ｼ繧ｹ・亥ｭ伜惠縺吶ｋ繝輔ぃ繧､繝ｫ縺ｮ縺ｿ・・
# ============================================================================
target_sources(Ambience PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PluginParameters.h
    Source/PluginParameters.cpp
    Source/PresetManager.h      # 笘・霑ｽ蜉
    Source/PresetManager.cpp    # 笘・霑ｽ蜉
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
)# ============================================================================
# 繧､繝ｳ繧ｯ繝ｫ繝ｼ繝峨ヱ繧ｹ
# ============================================================================
target_include_directories(Ambience PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)
# ============================================================================
# JUCE 繝｢繧ｸ繝･繝ｼ繝ｫ
# ============================================================================
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
    juce::juce_audio_processors
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
# ============================================================================
# 繧ｳ繝ｳ繝代う繝ｫ螳夂ｾｩ
# ============================================================================
target_compile_definitions(Ambience PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    AMBIENCE_VERSION="1.1.0"
)
# ============================================================================
# MSVC 繧ｳ繝ｳ繝代う繝ｫ繧ｪ繝励す繝ｧ繝ｳ
# ============================================================================
if(MSVC)
    target_compile_options(Ambience PRIVATE
        /arch:AVX2
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()
# ============================================================================
# JUCE 繝倥ャ繝繝ｼ逕滓・
# ============================================================================
juce_generate_juce_header(Ambience)
# ============================================================================
# 繝薙Ν繝峨Γ繝・そ繝ｼ繧ｸ
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Ambience VST3 - CMake Configuration")
message(STATUS "JUCE: C:/JUCE")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")
# ============================================================================
# テストハーネス (DSPTestRunner)
# ============================================================================
juce_add_console_app(DSPTestRunner)
target_sources(DSPTestRunner PRIVATE
    Source/DSP/DSPTestRunner.cpp
    Source/PluginParameters.h
    Source/PluginParameters.cpp
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
    Source/DSP/Saturator.h
    Source/DSP/OutputLimiter.h
    Source/DSP/OutputEQ.h
)
target_include_directories(DSPTestRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)
target_link_libraries(DSPTestRunner PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_audio_processors
)
if(MSVC)
    target_compile_options(DSPTestRunner PRIVATE
        /arch:AVX2 /utf-8 /W3 /wd4819 $<$<CONFIG:Release>:/O2 /fp:fast>
    )
endif()

juce_generate_juce_header(DSPTestRunner)

