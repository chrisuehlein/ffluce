#pragma once
#include <JuceHeader.h>
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "RenderTypes.h"
#include "FFmpegExecutor.h"
#include "ClipProcessor.h"
#include "TimelineAssembler.h"
#include "OverlayProcessor.h"
#include "AudioRenderer.h"

/**
 * Core render manager that coordinates the entire rendering pipeline.
 * Delegates specific tasks to specialized components.
 */
class RenderManagerCore : private juce::Thread,
                        private juce::Timer
{
public:
    // Use RenderTypes for shared types
    using RenderState = RenderTypes::RenderState;
    using VideoClipInfo = RenderTypes::VideoClipInfo;
    using OverlayClipInfo = RenderTypes::OverlayClipInfo;
    
    /**
     * Creates a new RenderManagerCore.
     * @param audioSource The binaural audio source to render from
     * @param filePlayer The file audio source to render from (optional)
     * @param noiseSource The noise audio source to render from (optional)
     */
    RenderManagerCore(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer = nullptr, NoiseAudioSource* noiseSource = nullptr);
    
    ~RenderManagerCore() override;
    
    /**
     * Starts the rendering process with the specified parameters.
     */
    bool startRendering(
        const juce::File& outputFile,
        const std::vector<VideoClipInfo>& introClips,
        const std::vector<VideoClipInfo>& loopClips,
        const std::vector<OverlayClipInfo>& overlayClips,
        double totalDuration,
        double fadeInDuration,
        double fadeOutDuration,
        std::function<void(const juce::String&)> statusCallback,
        std::function<void(double)> progressCallback,
        bool useNvidiaAcceleration = false,
        bool audioOnly = false,
        const juce::String& tempNvidiaParams = "",
        const juce::String& tempCpuParams = "",
        const juce::String& finalNvidiaParams = "",
        const juce::String& finalCpuParams = "");
    
    /** Cancels the current rendering process. */
    void cancelRendering();
    
    /** Returns the current state of the rendering process. */
    RenderState getState() const { return state; }
    
    /** Returns true if rendering is in progress. */
    bool isRendering() const { 
        return state != RenderState::Idle && 
               state != RenderState::Completed && 
               state != RenderState::Failed; 
    }
                                 
    /** Returns the current status message. */
    juce::String getStatusMessage() const { return currentStatusMessage; }

private:
    /** Thread run method that orchestrates the entire rendering process. */
    void run() override;
    
    /** Timer callback to check for progress updates from FFmpeg. */
    void timerCallback() override;
    
    /** Updates the current state and notifies the status callback. */
    void updateState(RenderState newState, const juce::String& statusMessage);
    
    /** Creates working directories for temporary files. */
    bool createWorkingDirectories();
    
    /** Cleans up all temporary files after rendering. */
    void cleanup();
    
    /** Prepares per-render logging directories and loggers. */
    void initialiseLoggingSession();
    
    /** Restores logging state after a render session ends. */
    void teardownLoggingSession();
    
    // Component instances
    std::unique_ptr<FFmpegExecutor> ffmpegExecutor;
    std::unique_ptr<ClipProcessor> clipProcessor;
    std::unique_ptr<TimelineAssembler> timelineAssembler;
    std::unique_ptr<OverlayProcessor> overlayProcessor;
    std::unique_ptr<AudioRenderer> audioRenderer;
    
    // Audio sources
    BinauralAudioSource* binauralSource;
    FilePlayerAudioSource* filePlayer;
    NoiseAudioSource* noiseSource;
    
    // Render parameters
    juce::File outputFile;
    std::vector<VideoClipInfo> introClips;
    std::vector<VideoClipInfo> loopClips;
    std::vector<OverlayClipInfo> overlayClips;
    double totalDuration;
    double fadeInDuration;
    double fadeOutDuration;
    
    // Callbacks
    std::function<void(const juce::String&)> statusCallback;
    std::function<void(double)> progressCallback;
    
    // Temporary directories and files
    juce::File tempDirectory;
    juce::File audioFile;
    
    // State tracking
    RenderState state;
    std::atomic<double> progress;
    std::atomic<bool> shouldCancel;
    bool useNvidiaAcceleration;
    bool audioOnly;
    juce::String currentStatusMessage;
    
    // Quality preset encoding parameters
    juce::String tempNvidiaParams;
    juce::String tempCpuParams;
    juce::String finalNvidiaParams;
    juce::String finalCpuParams;
    
    // Timing information
    juce::Time renderStartTime;
    juce::Time renderEndTime;
    
    // Get the elapsed render time in seconds
    double getElapsedRenderTimeSeconds() const;
    
    // Get human-readable elapsed time string (HH:MM:SS)
    juce::String getElapsedTimeString() const;
    
    // Log file for detailed debugging
    juce::File logFile;
    
    // Function for logging to both console and file
    std::function<void(const juce::String&)> logFunction;
    
    // Session logging infrastructure
    juce::File renderSessionDirectory;
    juce::File ffmpegLogDirectory;
    std::unique_ptr<juce::FileOutputStream> renderSessionLogStream;
    std::unique_ptr<juce::Logger> sessionConsoleLogger;
    juce::Logger* previousLogger = nullptr;
    juce::CriticalSection logWriteLock;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RenderManagerCore)
};
