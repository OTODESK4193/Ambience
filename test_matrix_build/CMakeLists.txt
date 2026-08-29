cmake_minimum_required(VERSION 3.22)
project(TestRework VERSION 1.0.0)
add_executable(TestRework ../TB/HighEnd_OptimizationTest.cpp)
target_compile_features(TestRework PRIVATE cxx_std_17)
