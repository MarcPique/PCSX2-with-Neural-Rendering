cmake_minimum_required(VERSION 3.21)
project(pcsx2_settings_only LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "The portable ReShade integration requires Windows")
endif()
# ReShade v6.8.0, matching fetch-neural-runtime.ps1. Download headers, never binaries.
include(FetchContent)
FetchContent_Declare(reshade_sdk
    GIT_REPOSITORY https://github.com/crosire/reshade.git
    GIT_TAG 18deaa52de0c425a78b329e9cb3c497281cd00ec
    GIT_SUBMODULES ""
    SOURCE_SUBDIR sdk-headers-only)
FetchContent_MakeAvailable(reshade_sdk)
add_library(pcsx2-settings-only SHARED settings_only.cpp)
target_include_directories(pcsx2-settings-only PRIVATE "${reshade_sdk_SOURCE_DIR}/include")
target_compile_features(pcsx2-settings-only PRIVATE cxx_std_20)
set_target_properties(pcsx2-settings-only PROPERTIES PREFIX "" SUFFIX ".addon64"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
configure_file("${reshade_sdk_SOURCE_DIR}/LICENSE.md" "ReShade-SDK-LICENSE.md" COPYONLY)
enable_testing()
add_executable(overlay-guard-test guard_test.cpp)
target_include_directories(overlay-guard-test PRIVATE "${reshade_sdk_SOURCE_DIR}/include")
target_compile_features(overlay-guard-test PRIVATE cxx_std_20)
set_target_properties(overlay-guard-test PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME overlay-guard COMMAND overlay-guard-test "$<TARGET_FILE:pcsx2-settings-only>")
