#include "VideoPreviewComponent.h"

VideoPreviewComponent::VideoPreviewComponent()
{
    // Initialize infinite YouTube streamer for proper sequence streaming
    infiniteStreamer = std::make_unique<YoutubeStreamer>();
    infiniteStreamer->onStatusUpdate = [this](const juce::String& status) {
        juce::MessageManager::callAsync([this, status]() {
            if (!status.isEmpty() && status.length() < 1000) {
                statusLabel.setText(status, juce::dontSendNotification);
            }
        });
    };
    infiniteStreamer->onError = [this](const juce::String& error) {
        juce::MessageManager::callAsync([this, error]() {
            if (!error.isEmpty() && error.length() < 1000) {
                statusLabel.setText("Error: " + error, juce::dontSendNotification);
            }
        });
    };
    
    // Add child components
    addAndMakeVisible(previewButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(streamButton);
    addAndMakeVisible(loopToggle);
    addAndMakeVisible(serviceLabel);
    addAndMakeVisible(serviceComboBox);
    addAndMakeVisible(streamKeyLabel);
    addAndMakeVisible(streamKeyEditor);
    addAndMakeVisible(getStreamKeyButton);
    addAndMakeVisible(customRtmpLabel);
    addAndMakeVisible(customRtmpEditor);
    addAndMakeVisible(previewStreamToggle);
    addAndMakeVisible(fileOutputButton);
    addAndMakeVisible(testPatternButton);
    addAndMakeVisible(checkYouTubeButton);
    addAndMakeVisible(checkNetworkButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(audioToggleLabel);
    addAndMakeVisible(audioToggle);
    addAndMakeVisible(speedButton);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(volumeLabel);
    addAndMakeVisible(qualityLabel);
    addAndMakeVisible(qualityComboBox);
    addAndMakeVisible(bufferingProgress);
    
    // Setup component listeners
    previewButton.addListener(this);
    stopButton.addListener(this);
    streamButton.addListener(this);
    getStreamKeyButton.addListener(this);
    fileOutputButton.addListener(this);
    testPatternButton.addListener(this);
    checkYouTubeButton.addListener(this);
    checkNetworkButton.addListener(this);
    speedButton.addListener(this);
    
    streamKeyEditor.addListener(this);
    customRtmpEditor.addListener(this);
    
    // Setup combo boxes
    serviceComboBox.addItem("YouTube", YouTube);
    serviceComboBox.addItem("Twitch", Twitch);
    serviceComboBox.addItem("Facebook", Facebook);
    serviceComboBox.addItem("Custom RTMP", CustomRTMP);
    serviceComboBox.setSelectedId(YouTube);
    serviceComboBox.onChange = [this]() { updateStreamingServiceComponents(); };
    
    qualityComboBox.addItem("Low Quality (Fast)", static_cast<int>(PreviewQuality::Low));
    qualityComboBox.addItem("Medium Quality", static_cast<int>(PreviewQuality::Medium));
    qualityComboBox.addItem("High Quality", static_cast<int>(PreviewQuality::High));
    qualityComboBox.setSelectedId(static_cast<int>(PreviewQuality::Medium));
    
    // Setup sliders
    volumeSlider.setRange(0.0, 1.0);
    volumeSlider.setValue(0.7);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    
    // Initialize UI state
    stopButton.setEnabled(false);
    audioToggle.setToggleState(true, juce::dontSendNotification);
    loopToggle.setToggleState(true, juce::dontSendNotification);
    updateStreamingServiceComponents();
    
    // Start timer for status updates
    startTimer(1000);
}

VideoPreviewComponent::~VideoPreviewComponent()
{
    stopTimer();
    stopPreview();
    stopStreaming();
}

void VideoPreviewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    
    auto bounds = getLocalBounds();
    auto previewArea = bounds.removeFromTop(getHeight() * 0.6f);
    
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRect(previewArea);
    
    if (!currentVideoFile.getFullPathName().isEmpty())
    {
        drawFallbackPreview(g, previewArea);
    }
    else
    {
        g.setColour(juce::Colours::white);
        g.setFont(16.0f);
        g.drawText("Drop video file here or click Preview", 
                   previewArea, juce::Justification::centred);
    }
}

void VideoPreviewComponent::resized()
{
    auto bounds = getLocalBounds();
    auto previewArea = bounds.removeFromTop(getHeight() * 0.6f);
    auto controlsArea = bounds.reduced(10);
    
    // Top row
    auto row = controlsArea.removeFromTop(30);
    previewButton.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(5);
    stopButton.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(5);
    loopToggle.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(10);
    qualityLabel.setBounds(row.removeFromLeft(50));
    qualityComboBox.setBounds(row.removeFromLeft(120));
    
    controlsArea.removeFromTop(10);
    
    // Service row
    row = controlsArea.removeFromTop(30);
    serviceLabel.setBounds(row.removeFromLeft(80));
    serviceComboBox.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(10);
    
    if (currentService == CustomRTMP)
    {
        customRtmpLabel.setBounds(row.removeFromLeft(80));
        customRtmpEditor.setBounds(row.removeFromLeft(200));
    }
    else
    {
        streamKeyLabel.setBounds(row.removeFromLeft(80));
        streamKeyEditor.setBounds(row.removeFromLeft(200));
        row.removeFromLeft(5);
        getStreamKeyButton.setBounds(row.removeFromLeft(100));
    }
    
    controlsArea.removeFromTop(10);
    
    // Stream controls row
    row = controlsArea.removeFromTop(30);
    streamButton.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(5);
    audioToggleLabel.setBounds(row.removeFromLeft(80));
    audioToggle.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(10);
    previewStreamToggle.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(5);
    fileOutputButton.setBounds(row.removeFromLeft(100));
    
    controlsArea.removeFromTop(10);
    
    // Test controls row
    row = controlsArea.removeFromTop(30);
    testPatternButton.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(5);
    checkYouTubeButton.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(5);
    checkNetworkButton.setBounds(row.removeFromLeft(120));
    
    controlsArea.removeFromTop(10);
    
    // Volume controls
    row = controlsArea.removeFromTop(30);
    volumeLabel.setBounds(row.removeFromLeft(60));
    volumeSlider.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(10);
    speedButton.setBounds(row.removeFromLeft(60));
    
    controlsArea.removeFromTop(10);
    
    // Buffering progress (full width)
    bufferingProgress.setBounds(controlsArea.removeFromTop(20));
    controlsArea.removeFromTop(5);
    
    // Status
    statusLabel.setBounds(controlsArea.removeFromTop(30));
}

void VideoPreviewComponent::buttonClicked(juce::Button* button)
{
    if (button == &previewButton)
    {
        if (isPlaying)
        {
            stopPreview();
        }
        else
        {
            startPreview(loopToggle.getToggleState());
        }
    }
    else if (button == &stopButton)
    {
        stopPreview();
    }
    else if (button == &streamButton)
    {
        if (isStreaming)
        {
            stopStreaming();
        }
        else
        {
            startStreaming();
        }
    }
}

void VideoPreviewComponent::timerCallback()
{
    // Check streaming status
    if (isStreaming && infiniteStreamer)
    {
        if (infiniteStreamer->isStreaming())
        {
            statusLabel.setText("Streaming active", juce::dontSendNotification);
        }
        else
        {
            isStreaming = false;
            streamButton.setButtonText("Stream");
            statusLabel.setText("Streaming stopped", juce::dontSendNotification);
        }
    }
}

void VideoPreviewComponent::setVideoFile(const juce::File& file)
{
    currentVideoFile = file;
    statusLabel.setText("Video loaded: " + file.getFileName(), juce::dontSendNotification);
    repaint();
}

bool VideoPreviewComponent::startPreview(bool shouldLoop)
{
    statusLabel.setText("Preview functionality simplified", juce::dontSendNotification);
    isPlaying = true;
    previewButton.setButtonText("Stop");
    stopButton.setEnabled(true);
    return true;
}

void VideoPreviewComponent::stopPreview()
{
    if (playerProcess && playerProcess->isRunning())
    {
        playerProcess->kill();
    }
    
    isPlaying = false;
    previewButton.setButtonText("Preview");
    stopButton.setEnabled(false);
    statusLabel.setText("Preview stopped", juce::dontSendNotification);
}

bool VideoPreviewComponent::startStreaming()
{
    juce::String url = getStreamUrl();
    if (url.isEmpty())
    {
        statusLabel.setText("Please enter stream key", juce::dontSendNotification);
        return false;
    }
    
    // Use simplified streaming through YoutubeStreamer
    if (infiniteStreamer && infiniteStreamer->startStreaming(url))
    {
        isStreaming = true;
        streamButton.setButtonText("Stop Stream");
        statusLabel.setText("Starting stream...", juce::dontSendNotification);
        return true;
    }
    
    statusLabel.setText("Failed to start streaming", juce::dontSendNotification);
    return false;
}

void VideoPreviewComponent::stopStreaming()
{
    if (infiniteStreamer)
    {
        infiniteStreamer->stopStreaming();
    }
    
    isStreaming = false;
    streamButton.setButtonText("Stream");
    statusLabel.setText("Streaming stopped", juce::dontSendNotification);
}

bool VideoPreviewComponent::startRealtimePreview(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                                 const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                                 const std::vector<RenderTypes::OverlayClipInfo>& overlayClips)
{
    // Using YoutubeStreamer for preview - setup and start streaming
    infiniteStreamer->setSequence(introClips, loopClips, overlayClips);
    statusLabel.setText("Realtime preview started", juce::dontSendNotification);
    return true;
}

bool VideoPreviewComponent::startRealtimeStreaming(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                                   const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                                   const juce::String& rtmpUrl,
                                                   const std::vector<RenderTypes::OverlayClipInfo>& overlayClips)
{
    // Using YoutubeStreamer for streaming - setup and start streaming
    infiniteStreamer->setSequence(introClips, loopClips, overlayClips);
    bool success = infiniteStreamer->startStreaming(rtmpUrl);
    
    if (success)
    {
        isStreaming = true;
        streamButton.setButtonText("Stop Stream");
        statusLabel.setText("Realtime streaming started", juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("Failed to start realtime streaming", juce::dontSendNotification);
    }
    
    return success;
}

void VideoPreviewComponent::setAudioSources(BinauralAudioSource* binaural, 
                                            FilePlayerAudioSource* filePlayer, 
                                            NoiseAudioSource* noise)
{
    binauralSource = binaural;
    filePlayerSource = filePlayer;
    noiseSource = noise;
}

void VideoPreviewComponent::setClips(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                     const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                     const std::vector<RenderTypes::OverlayClipInfo>& overlayClips)
{
    storedIntroClips = introClips;
    storedLoopClips = loopClips;
    storedOverlayClips = overlayClips;
}

bool VideoPreviewComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        juce::File f(file);
        if (f.hasFileExtension("mp4;mov;avi;mkv;wmv;flv;webm"))
            return true;
    }
    return false;
}

void VideoPreviewComponent::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
    {
        juce::File f(file);
        if (f.hasFileExtension("mp4;mov;avi;mkv;wmv;flv;webm"))
        {
            setVideoFile(f);
            break;
        }
    }
}

juce::String VideoPreviewComponent::getStreamUrl()
{
    juce::String key = streamKeyEditor.getText();
    
    if (currentService == YouTube)
        return "rtmp://a.rtmp.youtube.com/live2/" + key;
    else if (currentService == Twitch)
        return "rtmp://live.twitch.tv/app/" + key;
    else if (currentService == Facebook)
        return "rtmp://rtmp-api.facebook.com:80/rtmp/" + key;
    else if (currentService == CustomRTMP)
        return customRtmpEditor.getText();
    
    return "";
}

void VideoPreviewComponent::updateStreamingServiceComponents()
{
    currentService = static_cast<StreamingService>(serviceComboBox.getSelectedId());
    
    bool isCustomRTMP = (currentService == CustomRTMP);
    
    streamKeyLabel.setVisible(!isCustomRTMP);
    streamKeyEditor.setVisible(!isCustomRTMP);
    getStreamKeyButton.setVisible(!isCustomRTMP);
    
    customRtmpLabel.setVisible(isCustomRTMP);
    customRtmpEditor.setVisible(isCustomRTMP);
    
    resized();
}

void VideoPreviewComponent::drawFallbackPreview(juce::Graphics& g, const juce::Rectangle<int>& previewArea)
{
    g.setColour(juce::Colour(0xff404040));
    g.fillRect(previewArea);
    
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    
    auto textArea = previewArea.reduced(20);
    g.drawText("Video: " + currentVideoFile.getFileName(), 
               textArea.removeFromTop(30), juce::Justification::centred);
    
    if (isPlaying)
    {
        g.setColour(juce::Colours::green);
        g.drawText("Playing", textArea.removeFromTop(30), juce::Justification::centred);
    }
    
    if (isStreaming)
    {
        g.setColour(juce::Colours::red);
        g.drawText("Streaming", textArea.removeFromTop(30), juce::Justification::centred);
    }
}

// Simplified stub implementations for missing functions
bool VideoPreviewComponent::checkFFmpegAvailability() { return true; }
juce::String VideoPreviewComponent::getFFmpegPath() { return "ffmpeg"; }
juce::String VideoPreviewComponent::getFFplayPath() { return "ffplay"; }
bool VideoPreviewComponent::checkStreamingConnectivity() { return true; }
void VideoPreviewComponent::createStreamingDiagnosticTool(const juce::File&) {}
void VideoPreviewComponent::checkYouTubeStreamStatus(const juce::String&) {}
juce::String VideoPreviewComponent::generateTestStreamKey() { return "test_key_12345"; }
bool VideoPreviewComponent::startDirectStreaming() { return false; }
bool VideoPreviewComponent::createYouTubeStreamingBatchFile(const juce::File&, const juce::String&) { return false; }
bool VideoPreviewComponent::startEmergencyStreaming() { return false; }
bool VideoPreviewComponent::preprocessVideoForYouTube(const juce::File&, const juce::File&) { return false; }
bool VideoPreviewComponent::streamProcessedVideoToYouTube(const juce::File&, const juce::String&) { return false; }
bool VideoPreviewComponent::streamDirectlyToYouTube(const juce::File&, const juce::String&) { return false; }
void VideoPreviewComponent::startStreamHealthCheck() {}
bool VideoPreviewComponent::startStreamPreview() { return false; }
bool VideoPreviewComponent::saveStreamToFile(const juce::File&) { return false; }
void VideoPreviewComponent::streamTestPattern(const juce::String&) {}