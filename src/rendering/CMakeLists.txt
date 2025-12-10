# Add source files for the Rendering module
target_sources(${PROJECT_NAME}
    PRIVATE
        RenderManager.cpp
        RenderManagerCore.cpp
        FFmpegExecutor.cpp
        ClipProcessor.cpp
        TimelineAssembler.cpp
        OverlayProcessor.cpp
        AudioRenderer.cpp
)

# Add include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)