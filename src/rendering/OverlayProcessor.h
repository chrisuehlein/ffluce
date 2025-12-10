#pragma once
#include <JuceHeader.h>
#include "RenderTypes.h"
#include "FFmpegExecutor.h"

/**
 * Handles the processing and application of overlay clips to the main timeline.
 */
class OverlayProcessor
{
public:
    /**
     * Creates a new OverlayProcessor.
     * @param ffmpegExecutor The FFmpeg executor to use for overlay processing
     */
    OverlayProcessor(FFmpegExecutor* ffmpegExecutor);
    ~OverlayProcessor();
    
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
     * @param finalNvidiaParams NVIDIA encoding parameters for final output
     * @param finalCpuParams CPU encoding parameters for final output
     */
    void setEncodingParams(bool useNvidiaAcceleration, 
                          const juce::String& tempNvidiaParams,
                          const juce::String& tempCpuParams,
                          const juce::String& finalNvidiaParams,
                          const juce::String& finalCpuParams);
    
    /**
     * Processes overlay clips and applies them to the main timeline.
     * @param baseVideoFile The base video file to apply overlays to
     * @param overlayClips Information about the overlay clips
     * @param totalDuration The total duration of the video in seconds
     * @param tempDirectory The directory to store temporary files
     * @param outputFile The file to save the output to
     * @return true if the operation was successful, false otherwise
     */
    bool processOverlays(const juce::File& baseVideoFile,
                        const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                        double totalDuration,
                        const juce::File& tempDirectory,
                        const juce::File& outputFile);
    
private:
    /**
     * Prepares a single overlay clip for use.
     * @param overlayClip The overlay clip to prepare
     * @param tempDirectory The directory to store the prepared clip
     * @return The prepared overlay clip file
     */
    juce::File prepareOverlayClip(const RenderTypes::OverlayClipInfo& overlayClip,
                                const juce::File& tempDirectory);
    
    // FFmpeg executor for running commands
    FFmpegExecutor* ffmpegExecutor;
    
    // Encoding parameters
    bool useNvidiaAcceleration;
    juce::String tempNvidiaParams;
    juce::String tempCpuParams;
    juce::String finalNvidiaParams;
    juce::String finalCpuParams;
    
    // Log callback
    std::function<void(const juce::String&)> logCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayProcessor)
};