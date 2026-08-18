cmake_minimum_required(VERSION 3.22)
project(TB VERSION 1.0.0)
add_subdirectory(C:/JUCE ${CMAKE_BINARY_DIR}/JUCE)

juce_add_console_app(ERTest)
target_sources(ERTest PRIVATE
    D:/VST_Project/Ambience/TB/DSPTestRunner.cpp
    D:/VST_Project/Ambience/Source/DSP/BiquadFilters.cpp
    D:/VST_Project/Ambience/Source/DSP/MagnitudeResponseFitter.cpp
    D:/VST_Project/Ambience/Source/DSP/UniversalEngine.cpp
    D:/VST_Project/Ambience/Source/DSP/AcousticMetrics.cpp
)
target_include_directories(ERTest PRIVATE
    D:/VST_Project/Ambience/Source
    D:/VST_Project/Ambience/Source/DSP
)
target_link_libraries(ERTest PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
)
juce_generate_juce_header(ERTest)