cmake_minimum_required(VERSION 3.22)

# macOS build settings. These must be set before project().
# Universal binary (Apple Silicon + Intel) so the same build runs on the M4 Mac mini
# and on the Intel Mac. Deployment target 11.0 = macOS Big Sur or newer.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build for Apple Silicon and Intel")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(PhenomeEon VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PHE_BUILD_PLUGIN "Build the AU/VST3 plugin (needs JUCE)" ON)
option(PHE_BUILD_TESTS  "Build the engine self-tests" ON)

if(PHE_BUILD_TESTS)
    enable_testing()
endif()

# 1) The audio engine: plain C++, no plugin framework, reusable for later plugins.
add_subdirectory(engine)

# 2) The plugin wrapper and controls (JUCE).
if(PHE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()
