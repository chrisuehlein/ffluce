#pragma once
#include <JuceHeader.h>
#include "../rendering/RenderTypes.h"
#include "../rendering/TimelineAssembler.h"
#include "../rendering/FFmpegExecutor.h"
#include "../rendering/OverlayProcessor.h"
#include "StreamingDebugLogger.h"

/**
 * YoutubeStreamer
 * 
 * Real-time streaming to YouTube/Twitch/Custom RTMP with:
 * - Intro clips play ONCE
 * - Loop clips repeat INDEFINITELY with crossfades
 * - NVENC hardware acceleration
 * - Continuous streaming without interruption
 */
class YoutubeStreamer : public juce::Thread
{
public:
    YoutubeStreamer();
    ~YoutubeStreamer();
    
    // Thread implementation
    void run() override;
    
    // Configuration
    void setSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                    const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                    const std::vector<RenderTypes::OverlayClipInfo>& overlayClips);
    
    // Streaming control
    bool startStreaming(const juce::String& rtmpUrlOrKey, int platformId = 1, int bitrateKbps = 2500);
    void stopStreaming();
    bool isStreaming() const { return streamingActive; }
    
    // Callbacks
    std::function<void(const juce::String&)> onStatusUpdate;
    std::function<void(const juce::String&)> onError;
    std::function<void(const juce::String&)> onFFmpegOutput;  // For health monitoring
    
    // Audio streaming
    bool sendAudioData(const float* const* audioData, int numSamples, int numChannels);
    void setAudioFormat(double sampleRate, int blockSize);  // Configure audio format
    
private:
    // FFmpeg pipeline management
    bool setupFFmpegPipeline();
    void cleanupFFmpegPipeline();
    juce::String buildStreamingCommand();
    
    // Sequence creation using TimelineAssembler
    bool createStreamingSequences(const juce::File& tempDir);
    bool createStreamingSpecificSequences(const juce::File& tempDir, 
                                        const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                        const std::vector<RenderTypes::VideoClipInfo>& loopClips);
    bool buildSingleCycleLoopSequences(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                      const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                      const juce::File& tempDir);
    bool buildSingleCycleRawLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& loopClips, 
                                        const juce::File& tempDir,
                                        const juce::File& outputFile);
    bool buildLoopFromIntroSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                   const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                   const juce::File& loopSequenceRaw, const juce::File& tempDir);
    bool buildLoopFromLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                  const juce::File& loopSequenceRaw, const juce::File& tempDir);
    bool concatenateTwoFiles(const juce::File& file1, const juce::File& file2, const juce::File& outputFile);
    bool createSeamlessLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                   const juce::File& loopSequenceRaw, const juce::File& tempDir,
                                   const juce::File& outputFile);
    
    // Overlay application for streaming sequences
    bool applyOverlaysToSequence(const juce::File& inputSequence, 
                                const juce::File& outputSequence,
                                const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                double timeOffset);
    
    // Process overlays for all streaming sequences
    bool processOverlaysForStreaming(const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                    const juce::File& tempDir);
    bool createAudioPipe();
    void closeAudioPipe();
    
    // TimelineAssembler and its dependencies
    std::unique_ptr<FFmpegExecutor> ffmpegExecutor;
    std::unique_ptr<OverlayProcessor> overlayProcessor;
    std::unique_ptr<TimelineAssembler> timelineAssembler;
    
    // Clip data
    std::vector<RenderTypes::VideoClipInfo> introClips;
    std::vector<RenderTypes::VideoClipInfo> loopClips;
    std::vector<RenderTypes::OverlayClipInfo> overlayClips;
    
    // Streaming state
    std::atomic<bool> streamingActive{false};
    juce::String rtmpKey;
    juce::String rtmpUrl;  // Full RTMP URL
    int platform{1};      // 1=YouTube, 2=Twitch, 3=Custom
    int streamingBitrate{2500}; // Default 2.5 Mbps
    bool streamingUseNVENC{ true };
    std::unique_ptr<juce::ChildProcess> ffmpegProcess;
    
    // Thread state
    juce::String pendingRtmpKey;
    int pendingPlatform{1};
    
    // Audio streaming via named pipe
    juce::String audioPipeName;
    juce::String audioPipePath;
    std::unique_ptr<juce::NamedPipe> audioPipe;
    std::atomic<bool> audioPipeConnected { false };
    
    // Crossfade buffering to eliminate audio pops (10ms at 44.1kHz = 441 samples)
    static const int crossfadeSamples = 441; // 10ms at 44.1kHz
    // Track runtime metrics
    std::atomic<juce::int64> startTimeMs{0};
    // Debug logging for crash diagnosis
    std::unique_ptr<StreamingDebugLogger> debugLogger;
    
    // Memory and resource tracking
    std::atomic<size_t> totalBytesWritten{0};
    std::atomic<size_t> audioBufferWriteCount{0};
    std::atomic<int> ffmpegRestartCount{0};
    
    // Audio format settings
    double currentSampleRate{44100.0};
    int currentBlockSize{512};
    
    // Progress parsing for health monitoring
    void processProgressOutput(const juce::String& output);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YoutubeStreamer)
};
