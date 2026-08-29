cmake_minimum_required(VERSION 3.22)
project(WaveGen VERSION 1.0.0)
add_executable(WaveGen ../TB/WaveGen_5metrics.cpp)
target_compile_features(WaveGen PRIVATE cxx_std_17)
