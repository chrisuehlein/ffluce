#pragma once
#include <JuceHeader.h>
#include "../../core/ProcessManager.h"
#include "../../streaming/YoutubeStreamer.h"
#include "../../audio/BinauralAudioSource.h"
#include "../../audio/FilePlayerAudioSource.h"
#include "../../audio/NoiseAudioSource.h"

/**
 * VideoPreviewComponent
 * 
 * A component that provides video preview functionality:
 * - Integrated lightweight video preview using FFplay in a more optimized way
 * - Stream button to stream the current video via RTMP to a specified URL
 * - Control panel for changing streaming settings
 */
class VideoPreviewComponent : public juce::Component,
                              private juce::Button::Listener,
                              private juce::TextEditor::Listener,
                              private juce::Timer,
                              private juce::FileDragAndDropTarget
{
public:
    VideoPreviewComponent();
    ~VideoPreviewComponent() override;
    
    // Set the file to preview
    void setVideoFile(const juce::File& file);
    
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // File drag and drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    
    // Get/set the RTMP URL for streaming
    void setRtmpUrl(const juce::String& url) { rtmpUrl = url; }
    juce::String getRtmpUrl() const { return rtmpUrl; }
    
    // Returns true if streaming is active
    bool isCurrentlyStreaming() const { return isStreaming; }
    
    // Returns the status message
    juce::String getStatusMessage() const { return statusLabel.getText(); }
    
    // Start immediate preview with optional loop setting
    bool startPreview(bool shouldLoop = true);
    
    // Start realtime preview of intro->loop sequence
    bool startRealtimePreview(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                             const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                             const std::vector<RenderTypes::OverlayClipInfo>& overlayClips = {});
    
    // Start realtime streaming of intro->loop sequence
    bool startRealtimeStreaming(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                               const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                               const juce::String& rtmpUrl,
                               const std::vector<RenderTypes::OverlayClipInfo>& overlayClips = {});
    
    // Set audio sources for realtime preview
    void setAudioSources(BinauralAudioSource* binaural, 
                        FilePlayerAudioSource* filePlayer, 
                        NoiseAudioSource* noise);
    
    // Stop preview playback
    void stopPreview();
    
    // Set clips for use by the preview button (called by VideoPanel)
    void setClips(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                  const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                  const std::vector<RenderTypes::OverlayClipInfo>& overlayClips = {});
    
public:
    // Stop streaming (public to allow video panel to stop it) 
    void stopStreaming();
    
    // Stream a test pattern instead of a video file
    void streamTestPattern(const juce::String& patternType);
    
private:
    // Button handler
    void buttonClicked(juce::Button* button) override;
    
    // TextEditor callbacks
    void textEditorTextChanged(juce::TextEditor&) override {}
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override {
        if (&editor == &streamKeyEditor) {
            // Process the stream key when Enter is pressed
            rtmpUrl = getStreamUrl();
        }
    }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override {}
    void textEditorFocusLost(juce::TextEditor&) override {}
    
    // Custom method to handle stream key right-click
    void showStreamKeyContextMenu() {
        // Create a popup menu directly
        juce::PopupMenu menu;
        menu.addItem(1, "Generate Test Stream Key");
        menu.addSeparator();
        menu.addItem(2, "Clear");
        
        // Get mouse position relative to the stream key editor
        auto position = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
        
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                        juce::Rectangle<int>(position.x, position.y, 1, 1)),
            [this](int result) {
                if (result == 1) {
                    // Generate test key
                    streamKeyEditor.setText(generateTestStreamKey());
                }
                else if (result == 2) {
                    // Clear the field
                    streamKeyEditor.clear();
                }
            });
    }
    
    // Timer callback to check player/streamer status
    void timerCallback() override;
    
    // Helper to check if FFmpeg is available
    bool checkFFmpegAvailability();
    
    // Helper to get FFmpeg path (reusing logic from the render system)
    juce::String getFFmpegPath();
    
    // Helper to get FFplay path
    juce::String getFFplayPath();
    
    // Check network connectivity to YouTube RTMP servers
    bool checkStreamingConnectivity();
    
    // Create a standalone diagnostic tool for YouTube streaming
    void createStreamingDiagnosticTool(const juce::File& destinationDir);
    
    // Check YouTube account streaming status - optional API key needed
    void checkYouTubeStreamStatus(const juce::String& apiKey = "");
    
    // Generate a test stream key for troubleshooting
    juce::String generateTestStreamKey();
    
    // Start streaming to RTMP (two-step process with preprocessing)
    bool startStreaming();
    
    // Start direct streaming to RTMP (fallback method when preprocessing fails)
    bool startDirectStreaming();
    
    // Create a standalone batch file for direct YouTube streaming
    bool createYouTubeStreamingBatchFile(const juce::File& videoFile, const juce::String& streamKey);
    
    // EMERGENCY direct streaming with minimal parameters (last resort when all else fails)
    bool startEmergencyStreaming();
    
    // Fixed preprocessing function that properly handles Windows paths and FFmpeg parameters
    bool preprocessVideoForYouTube(const juce::File& inputFile, const juce::File& outputFile);
    
    // Fixed streaming function that properly handles Windows paths and FFmpeg parameters
    bool streamProcessedVideoToYouTube(const juce::File& videoFile, const juce::String& streamKey);
    
    // Improved direct streaming function that streams directly without preprocessing
    bool streamDirectlyToYouTube(const juce::File& videoFile, const juce::String& streamUrl);
    
    // Start monitoring stream health
    void startStreamHealthCheck();
    
    // Start a preview of the current stream using FFplay
    bool startStreamPreview();
    
    // Save stream output directly to a file instead of RTMP
    bool saveStreamToFile(const juce::File& outputFile);
    
    // Helper method to draw fallback preview when thumbnail isn't available
    void drawFallbackPreview(juce::Graphics& g, const juce::Rectangle<int>& previewArea);
    
    // Helper method to get the complete RTMP URL based on service selection and stream key
    juce::String getStreamUrl();
    
    // Helper method to update visibility of streaming input fields based on selected service
    void updateStreamingServiceComponents();
    
    // UI components
    juce::TextButton previewButton { "Preview" };
    juce::ToggleButton loopToggle { "Loop" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton streamButton { "Stream" };
    
    // Streaming service components
    juce::Label serviceLabel { {}, "Service:" };
    juce::ComboBox serviceComboBox;
    juce::Label streamKeyLabel { {}, "Stream Key:" };
    juce::TextEditor streamKeyEditor;
    juce::TextButton getStreamKeyButton { "Get New Key" };
    juce::Label customRtmpLabel { {}, "RTMP URL:" };
    juce::TextEditor customRtmpEditor;
    
    juce::ToggleButton previewStreamToggle { "Show Stream" };
    juce::TextButton fileOutputButton { "Save Stream" };
    juce::TextButton testPatternButton { "Use Test Pattern" };
    juce::TextButton checkYouTubeButton { "Check YouTube Status" };
    juce::TextButton checkNetworkButton { "Create Diagnostic Tool" };
    juce::Label statusLabel { {}, "Ready" };
    juce::Label audioToggleLabel { {}, "Include Audio:" };
    juce::ToggleButton audioToggle { "Audio" };
    
    // Preview controls
    juce::TextButton speedButton { "1x" };
    juce::Slider volumeSlider;
    juce::Label volumeLabel { {}, "Volume:" };
    
    // Preview quality controls
    juce::Label qualityLabel { {}, "Quality:" };
    juce::ComboBox qualityComboBox;
    double bufferProgressValue = 0.0;
    juce::ProgressBar bufferingProgress { bufferProgressValue };
    
    // Preview quality enum (local definition)
    enum class PreviewQuality
    {
        Low = 1,     // 480p, low bitrate, fast processing
        Medium = 2,  // 720p, medium bitrate
        High = 3     // 1080p, high bitrate, full quality
    };
    
    // Streaming service types
    enum StreamingService
    {
        YouTube = 1,
        Twitch = 2,
        Facebook = 3,
        CustomRTMP = 4
    };
    
    // State
    juce::File currentVideoFile;
    juce::String rtmpUrl = ""; // Will be constructed from service and key
    StreamingService currentService = YouTube;
    bool isStreaming = false;
    bool isPlaying = false;
    bool includeAudio = true;
    bool bufferingCompletedAndLaunched = false; // Track if we've already launched FFplay after buffering
    float playbackSpeed = 1.0f;
    juce::int64 streamStartTime = 0;     // Timestamp when streaming started
    int streamTimeoutSeconds = 60;       // Timeout for stream startup
    
    // Child processes for FFplay and FFmpeg streaming using ManagedChildProcess
    std::unique_ptr<ManagedChildProcess> playerProcess;
    std::unique_ptr<ManagedChildProcess> streamerProcess;
    std::unique_ptr<ManagedChildProcess> streamPreviewProcess;
    
    // Realtime preview manager for intro->loop sequences
    // std::unique_ptr<RealTimePreviewManager> realtimePreviewManager; // REMOVED
    
    // YouTube streamer for proper sequence streaming
    std::unique_ptr<YoutubeStreamer> infiniteStreamer;
    
    // Audio sources for realtime preview
    BinauralAudioSource* binauralSource = nullptr;
    FilePlayerAudioSource* filePlayerSource = nullptr;
    NoiseAudioSource* noiseSource = nullptr;
    
    // Current frame for realtime preview display
    std::unique_ptr<juce::Image> currentRealtimeFrame;
    juce::CriticalSection frameLock;
    
    // Clips for realtime preview (set by VideoPanel)
    std::vector<RenderTypes::VideoClipInfo> storedIntroClips;
    std::vector<RenderTypes::VideoClipInfo> storedLoopClips;
    std::vector<RenderTypes::OverlayClipInfo> storedOverlayClips;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoPreviewComponent)
};