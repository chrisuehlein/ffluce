#pragma once
#include <JuceHeader.h>
#include "RenderTypes.h"
#include "FFmpegExecutor.h"

/**
 * Handles all video clip preparation, including duration adjustments,
 * format standardization, and split operations for crossfades.
 */
class ClipProcessor
{
public:
    /**
     * Creates a new ClipProcessor.
     * @param ffmpegExecutor The FFmpeg executor to use for clip processing
     */
    ClipProcessor(FFmpegExecutor* ffmpegExecutor);
    ~ClipProcessor();
    
    /**
     * Sets a callback for receiving log messages.
     * @param logCallback Function called with log messages
     */
    void setLogCallback(std::function<void(const juce::String&)> logCallback);
    
    /**
     * Sets the encoding parameters.
     * @param useNvidiaAcceleration Whether to use NVIDIA acceleration
     * @param tempNvidiaParams NVIDIA encoding parameters for temporary files
     * @param tempCpuParams CPU encoding parameters for temporary files
     */
    void setEncodingParams(bool useNvidiaAcceleration, 
                          const juce::String& tempNvidiaParams,
                          const juce::String& tempCpuParams);
    
    /**
     * Prepares video clips with exact durations.
     * @param sourceClips The source clips to process
     * @param tempDirectory The directory to store processed clips
     * @param tempVideoFiles Output vector to store the processed clip files
     * @return true if the operation was successful, false otherwise
     */
    bool prepareVideoClips(const std::vector<RenderTypes::VideoClipInfo>& sourceClips,
                          const juce::File& tempDirectory,
                          std::vector<juce::File>& tempVideoFiles);
    
    /**
     * Splits clips for crossfade processing.
     * @param introClips The intro clips info
     * @param loopClips The loop clips info
     * @param tempVideoFiles The prepared video files
     * @param tempDirectory The directory to store split clips
     * @param tempSplitFiles Output vector to store the split clip files
     * @return true if the operation was successful, false otherwise
     */
    bool splitClipsForCrossfades(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                               const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                               const std::vector<juce::File>& tempVideoFiles,
                               const juce::File& tempDirectory,
                               std::vector<juce::File>& tempSplitFiles);
    
    /**
     * Gets the duration of a video clip in seconds.
     * @param clip The video clip file
     * @return The duration in seconds
     */
    double getClipDuration(const juce::File& clip);
    
    /**
     * Processes a single video clip with exact duration.
     * @param sourceClip The source clip to process
     * @param startTime The start time within the source clip
     * @param duration The duration to extract
     * @param outputFile The file to save the processed clip to
     * @return true if the operation was successful, false otherwise
     */
    bool processClip(const juce::File& sourceClip, 
                    double startTime, 
                    double duration,
                    const juce::File& outputFile);
    
private:
    // FFmpeg executor for running commands
    FFmpegExecutor* ffmpegExecutor;
    
    // Encoding parameters
    bool useNvidiaAcceleration;
    juce::String tempNvidiaParams;
    juce::String tempCpuParams;
    
    // Log callback
    std::function<void(const juce::String&)> logCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipProcessor)
};