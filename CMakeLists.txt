cmake_minimum_required(VERSION 3.20)

project(FFLUCE
    VERSION 1.0.0
    DESCRIPTION "Audio/video assembler with FFmpeg/NVENC support"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(FFLUCE_ENABLE_NVENC "Enable NVIDIA hardware encoding support" ON)

# -----------------------------------------------------------------------------
# JUCE
# -----------------------------------------------------------------------------
# Option 1: JUCE as submodule in juce/
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/juce/CMakeLists.txt")
    add_subdirectory(juce)
# Option 2: FetchContent
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.1
        GIT_SHALLOW TRUE
    )
    # Disable JUCE extras to speed up build
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
# FFmpeg (external - used via process spawning, not linking)
# -----------------------------------------------------------------------------
# FFmpeg is spawned as a subprocess, so we just need to find the executables
# for validation. The actual path is configured at runtime.
if(DEFINED FFLUCE_FFMPEG_ROOT)
    set(FFMPEG_HINT_PATH "${FFLUCE_FFMPEG_ROOT}/bin")
elseif(DEFINED ENV{FFLUCE_FFMPEG_ROOT})
    set(FFMPEG_HINT_PATH "$ENV{FFLUCE_FFMPEG_ROOT}/bin")
else()
    set(FFMPEG_HINT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin")
endif()

find_program(FFMPEG_EXECUTABLE ffmpeg HINTS ${FFMPEG_HINT_PATH})
find_program(FFPROBE_EXECUTABLE ffprobe HINTS ${FFMPEG_HINT_PATH})

if(FFMPEG_EXECUTABLE)
    message(STATUS "Found FFmpeg: ${FFMPEG_EXECUTABLE}")
else()
    message(WARNING "FFmpeg not found. Run scripts/ffmpeg_fetch to download, or set FFLUCE_FFMPEG_ROOT")
endif()

if(FFPROBE_EXECUTABLE)
    message(STATUS "Found FFprobe: ${FFPROBE_EXECUTABLE}")
endif()

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
set(FFLUCE_SOURCES
    # core
    src/core/Main.cpp
    src/core/MainComponent.cpp
    src/core/MainComponent.h
    src/core/ProcessManager.h
    src/core/RenderDialog.h

    # audio
    src/audio/BinauralAudioSource.h
    src/audio/FilePlayerAudioSource.h
    src/audio/NoiseAudioSource.h
    src/audio/NoiseAudioSource.cpp

    # rendering
    src/rendering/RenderManager.h
    src/rendering/RenderManager.cpp
    src/rendering/RenderManagerCore.h
    src/rendering/RenderManagerCore.cpp
    src/rendering/RenderTypes.h
    src/rendering/FFmpegExecutor.h
    src/rendering/FFmpegExecutor.cpp
    src/rendering/TimelineAssembler.h
    src/rendering/TimelineAssembler.cpp
    src/rendering/ClipProcessor.h
    src/rendering/ClipProcessor.cpp
    src/rendering/OverlayProcessor.h
    src/rendering/OverlayProcessor.cpp
    src/rendering/AudioRenderer.h
    src/rendering/AudioRenderer.cpp

    # streaming
    src/streaming/YoutubeStreamer.h
    src/streaming/YoutubeStreamer.cpp
    src/streaming/StreamingDebugLogger.h

    # ui - panels
    src/ui/panels/AudioPanel.h
    src/ui/panels/VideoPanel.h
    src/ui/panels/VideoPanel.cpp
    src/ui/panels/VideoPreviewComponent.h
    src/ui/panels/VideoPreviewComponent.cpp

    # ui - components
    src/ui/components/AudioTrackComponent.h
    src/ui/components/BinauralTrackComponent.h
    src/ui/components/BinauralTrackComponent.cpp
    src/ui/components/FileTrackComponent.h
    src/ui/components/FileTrackComponent.cpp
    src/ui/components/NoiseTrackComponent.h
    src/ui/components/NoiseTrackComponent.cpp

    # ui - resources
    src/ui/resources/ImageResources.h

    # utils
    src/utils/RenderManager.h
    src/utils/RenderImpl.cpp
)

# -----------------------------------------------------------------------------
# JUCE Application Target
# -----------------------------------------------------------------------------
juce_add_gui_app(FFLUCE
    PRODUCT_NAME "FFLUCE"
    COMPANY_NAME "FFLUCE"
    BUNDLE_ID "com.ffluce.app"
    VERSION "${PROJECT_VERSION}"
)

target_sources(FFLUCE PRIVATE ${FFLUCE_SOURCES})

target_include_directories(FFLUCE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/src/streaming
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

target_compile_definitions(FFLUCE PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FFLUCE,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FFLUCE,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

if(FFLUCE_ENABLE_NVENC)
    target_compile_definitions(FFLUCE PRIVATE FFLUCE_ENABLE_NVENC=1)
endif()

target_link_libraries(FFLUCE PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_video
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------------
# Platform-specific settings
# -----------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(FFLUCE PRIVATE
        _UNICODE
        UNICODE
    )
endif()

# -----------------------------------------------------------------------------
# Binary Data (optional - for embedded resources)
# -----------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/images")
    file(GLOB_RECURSE BINARY_RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/*"
    )
    if(BINARY_RESOURCES)
        juce_add_binary_data(FFLUCE_BinaryData
            SOURCES ${BINARY_RESOURCES}
        )
        target_link_libraries(FFLUCE PRIVATE FFLUCE_BinaryData)
    endif()
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "FFLUCE Configuration Summary")
message(STATUS "============================")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  NVENC:        ${FFLUCE_ENABLE_NVENC}")
message(STATUS "  FFmpeg:       ${FFMPEG_EXECUTABLE}")
message(STATUS "")
