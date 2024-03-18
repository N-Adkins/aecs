cmake_minimum_required(VERSION 3.22)

project(
    aecs
    VERSION 0.1
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(aecs INTERFACE)

target_include_directories(aecs INTERFACE include)
target_compile_features(aecs INTERFACE cxx_std_11)
set_target_properties(aecs PROPERTIES CXX_EXTENSIONS OFF)
