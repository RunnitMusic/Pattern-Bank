cmake_minimum_required(VERSION 3.22)

project(StepShaperFL VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(STEPSHAPER_BUILD_FL_PLUGIN "Build the native FL Studio controller DLL" ON)
option(STEPSHAPER_BUILD_PREVIEW "Build the standalone UI preview" ON)
option(STEPSHAPER_BUILD_TESTS "Build core tests" ON)

add_library(StepShaperCore STATIC
    Source/Core.cpp
    Source/Core.h)
target_include_directories(StepShaperCore PUBLIC Source)
target_compile_features(StepShaperCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(StepShaperCore PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(StepShaperCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(STEPSHAPER_BUILD_FL_PLUGIN OR STEPSHAPER_BUILD_PREVIEW)
    set(STEPSHAPER_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH
        "Optional path to a local JUCE checkout")
    if(EXISTS "${STEPSHAPER_JUCE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${STEPSHAPER_JUCE_SOURCE_DIR}" JUCE EXCLUDE_FROM_ALL)
    else()
        message(STATUS "Local JUCE checkout not found; fetching JUCE 8.0.14")
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.14
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    add_library(StepShaperUI STATIC
        Source/PatternEditor.cpp
        Source/PatternEditor.h
        Source/StepShaperEditor.cpp
        Source/StepShaperEditor.h)
    target_include_directories(StepShaperUI PUBLIC Source)
    target_link_libraries(StepShaperUI
        PUBLIC StepShaperCore
        PRIVATE juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(StepShaperUI PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
endif()

if(STEPSHAPER_BUILD_FL_PLUGIN)
    if(NOT WIN32 AND NOT APPLE)
        message(FATAL_ERROR "The native FL Studio SDK target supports Windows and macOS")
    endif()

    add_library(StepShaperFL SHARED
        Source/FLPlugin.cpp
        Source/FLPlugin.h
        Source/FLPluginEntry.cpp
        third_party/fpsdk/src/cxx/fp_plugclass.cpp)
    target_include_directories(StepShaperFL PRIVATE
        Source
        third_party/fpsdk/src/cxx)
    target_link_libraries(StepShaperFL PRIVATE
        StepShaperCore
        StepShaperUI
        juce::juce_gui_basics
        juce::juce_gui_extra)
    target_compile_definitions(StepShaperFL PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>)
    set_target_properties(StepShaperFL PROPERTIES
        PREFIX "")
    set(STEPSHAPER_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/Pattern Bank")
    if(WIN32)
        set_target_properties(StepShaperFL PROPERTIES OUTPUT_NAME "Pattern Bank")
        set(STEPSHAPER_PACKAGED_BINARY "${STEPSHAPER_PACKAGE_DIR}/Pattern Bank_x64.dll")
    else()
        set_target_properties(StepShaperFL PROPERTIES OUTPUT_NAME "Pattern Bank_x64" SUFFIX ".dylib")
        set(STEPSHAPER_PACKAGED_BINARY "${STEPSHAPER_PACKAGE_DIR}/Pattern Bank_x64.dylib")
    endif()
    add_custom_command(TARGET StepShaperFL POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STEPSHAPER_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:StepShaperFL>"
            "${STEPSHAPER_PACKAGED_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/Plugin.nfo"
            "${STEPSHAPER_PACKAGE_DIR}/Plugin.nfo"
        VERBATIM)
endif()

if(STEPSHAPER_BUILD_PREVIEW)
    juce_add_gui_app(StepShaperPreview PRODUCT_NAME "Pattern Bank Preview")
    target_sources(StepShaperPreview PRIVATE Source/PreviewMain.cpp)
    target_link_libraries(StepShaperPreview PRIVATE
        StepShaperCore
        StepShaperUI
        juce::juce_gui_basics
        juce::juce_gui_extra)
    target_compile_definitions(StepShaperPreview PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
endif()

if(STEPSHAPER_BUILD_TESTS)
    enable_testing()
    add_executable(StepShaperCoreTests Tests/CoreTests.cpp)
    target_link_libraries(StepShaperCoreTests PRIVATE StepShaperCore)
    add_test(NAME StepShaperCoreTests COMMAND StepShaperCoreTests)
    add_executable(StepShaperCoreBenchmark Tests/CoreBenchmark.cpp)
    target_link_libraries(StepShaperCoreBenchmark PRIVATE StepShaperCore)
endif()
