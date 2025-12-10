/*
  ==============================================================================
    MainComponent.cpp - Central UI Coordinator
  ==============================================================================
*/

#include "MainComponent.h"
#include "../utils/RenderManager.h"
#include "../streaming/YoutubeStreamer.h"
#include "RenderDialog.h"

namespace
{
    constexpr float kStreamingLimiterThreshold = 0.891f; // -1 dB in linear gain

    void applyLimiterToBuffer(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        if (buffer.getNumChannels() == 0 || numSamples <= 0)
            return;

        startSample = juce::jlimit(0, buffer.getNumSamples(), startSample);
        numSamples = juce::jmin(numSamples, buffer.getNumSamples() - startSample);
        if (numSamples <= 0)
            return;

        float peakLevel = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            peakLevel = juce::jmax(peakLevel, buffer.getMagnitude(channel, startSample, numSamples));

        if (peakLevel <= kStreamingLimiterThreshold || peakLevel <= 0.0f)
            return;

        const float gain = kStreamingLimiterThreshold / peakLevel;
        buffer.applyGain(startSample, numSamples, gain);
    }
}

MainComponent::MainComponent()
{
    // Initialize audio sources for local playback
    binauralSource = std::make_unique<BinauralAudioSource>();
    filePlayer = std::make_unique<FilePlayerAudioSource>();
    noiseSource = std::make_unique<NoiseAudioSource>();

    // Separate audio sources for streaming (always active)
    streamingBinauralSource = std::make_unique<BinauralAudioSource>();
    streamingFilePlayer = std::make_unique<FilePlayerAudioSource>();
    streamingNoiseSource = std::make_unique<NoiseAudioSource>();

    // Start silent
    binauralSource->setGain(0.0f);
    noiseSource->setMuted(true);

    // Connect sources to UI
    audioPanel.setSources(binauralSource.get(), filePlayer.get(), noiseSource.get(),
                          streamingBinauralSource.get(), streamingFilePlayer.get(), streamingNoiseSource.get());

    // Section labels
    juce::Font sectionFont;
    sectionFont.setHeight(18.0f);
    sectionFont = sectionFont.boldened();

    videoSectionLabel.setFont(sectionFont);
    videoSectionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    videoSectionLabel.setJustificationType(juce::Justification::centredLeft);

    audioSectionLabel.setFont(sectionFont);
    audioSectionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    audioSectionLabel.setJustificationType(juce::Justification::centredLeft);

    // Duration editors (HH:MM:SS)
    auto configureTimeEditor = [](juce::TextEditor& editor, const juce::String& defaultText) {
        editor.setText(defaultText);
        editor.setInputRestrictions(2, "0123456789");
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
        editor.setColour(juce::TextEditor::textColourId, juce::Colours::black);
        editor.setColour(juce::CaretComponent::caretColourId, juce::Colours::black);
        editor.setColour(juce::TextEditor::highlightColourId, juce::Colours::lightblue);
        editor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
        editor.setJustification(juce::Justification::centred);
    };

    configureTimeEditor(hoursEditor, "0");
    configureTimeEditor(minutesEditor, "10");
    configureTimeEditor(secondsEditor, "0");

    hoursLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    hoursLabel.setJustificationType(juce::Justification::centredLeft);
    minutesLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    minutesLabel.setJustificationType(juce::Justification::centredLeft);
    secondsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    secondsLabel.setJustificationType(juce::Justification::centredLeft);

    fadeInEditor.setText("0.1");
    fadeInEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    fadeInEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    fadeInEditor.setColour(juce::TextEditor::textColourId, juce::Colours::black);
    fadeInEditor.setColour(juce::CaretComponent::caretColourId, juce::Colours::black);
    fadeInEditor.setColour(juce::TextEditor::highlightColourId, juce::Colours::lightblue);
    fadeInEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    
    fadeOutEditor.setText("2");
    fadeOutEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    fadeOutEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    fadeOutEditor.setColour(juce::TextEditor::textColourId, juce::Colours::black);
    fadeOutEditor.setColour(juce::CaretComponent::caretColourId, juce::Colours::black);
    fadeOutEditor.setColour(juce::TextEditor::highlightColourId, juce::Colours::lightblue);
    fadeOutEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);

    // Configure and style render button
    renderButton.setColour(juce::TextButton::buttonColourId, juce::Colour(70, 70, 70)); // Dark gray
    renderButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    
    // Configure save/load buttons
    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 60, 60)); // Dark gray
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 60, 60)); // Dark gray
    loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    
    // Configure progress area
    progressMessages.setMultiLine(true);
    progressMessages.setReadOnly(true);
    progressMessages.setCaretVisible(false);
    progressMessages.setColour(juce::TextEditor::backgroundColourId, juce::Colour(70, 70, 70)); // Dark gray background
    progressMessages.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey); // Light grey text for visibility
    progressMessages.setColour(juce::TextEditor::outlineColourId, juce::Colour(60, 60, 60)); // Darker gray outline
    
    // Transport buttons
    transportState = Stopped;

    // Try to load button images from various locations
    juce::File executableDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    auto findImage = [&](const juce::String& filename) -> juce::File {
        juce::Array<juce::File> locations = {
            executableDir.getChildFile(filename),
            executableDir.getParentDirectory().getChildFile(filename),
            executableDir.getParentDirectory().getParentDirectory().getChildFile(filename),
            juce::File::getCurrentWorkingDirectory().getChildFile(filename)
        };
        for (auto& file : locations)
            if (file.existsAsFile())
                return file;
        return {};
    };

    juce::File playFile = findImage("play.png");
    juce::File stopFile = findImage("stop.png");

    if (playFile.existsAsFile())
    {
        playImage = juce::ImageFileFormat::loadFrom(playFile);
        if (playImage.isValid())
            playPauseButton.setImage(playImage);
        else
            playPauseButton.setButtonText("PLAY");
    }
    else
    {
        playPauseButton.setButtonText("PLAY");
    }

    if (stopFile.existsAsFile())
    {
        stopImage = juce::ImageFileFormat::loadFrom(stopFile);
        if (stopImage.isValid())
            stopButton.setImage(stopImage);
        else
            stopButton.setButtonText("STOP");
    }
    else
    {
        stopButton.setButtonText("STOP");
    }

    playPauseButton.setTooltip("Play/Pause");
    playPauseButton.setComponentID("playPauseButton");
    stopButton.setTooltip("Stop");
    stopButton.setComponentID("stopButton");

    playPauseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 60, 60));
    playPauseButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(80, 80, 80));
    playPauseButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    playPauseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 50, 50));
    stopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(70, 70, 70));
    stopButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    stopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    playPauseButton.setSize(32, 32);
    stopButton.setSize(32, 32);
    
    // App logo - try embedded first, then file
    auto logoImage = ImageResources::getLogo();
    if (!logoImage.isValid())
    {
        juce::File logoFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                .getParentDirectory().getChildFile("logo.png");
        if (logoFile.existsAsFile())
            logoImage = juce::ImageFileFormat::loadFrom(logoFile);
    }

    if (logoImage.isValid())
        appLogoComponent.setImage(logoImage, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    
    // Configure presets section
    juce::Font presetsFont;
    presetsFont.setHeight(16.0f);
    presetsFont = presetsFont.boldened();
    presetsLabel.setFont(presetsFont);
    presetsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    presetsLabel.setJustificationType(juce::Justification::centredRight);
    
    // Configure output header
    juce::Font outputFont;
    outputFont.setHeight(18.0f);
    outputFont = outputFont.boldened();
    outputLabel.setFont(outputFont);
    outputLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    outputLabel.setJustificationType(juce::Justification::centredLeft);
    
    // Configure streaming header to match other headers
    juce::Font streamingFont;
    streamingFont.setHeight(18.0f);
    streamingFont = streamingFont.boldened();
    streamingLabel.setFont(streamingFont);
    streamingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    streamingLabel.setJustificationType(juce::Justification::centredLeft);
    
    // Make sure all property labels are visible
    durationLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    fadeInLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    fadeOutLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    
    // Force input text colors to black - try alternative approach
    hoursEditor.applyColourToAllText(juce::Colours::black, true);
    minutesEditor.applyColourToAllText(juce::Colours::black, true);
    secondsEditor.applyColourToAllText(juce::Colours::black, true);
    fadeInEditor.applyColourToAllText(juce::Colours::black, true);
    fadeOutEditor.applyColourToAllText(juce::Colours::black, true);
    
    // Add all components
    // Configure date label
    juce::Font dateFont;
    dateFont.setHeight(14.0f);
    dateLabel.setFont(dateFont);
    dateLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    dateLabel.setJustificationType(juce::Justification::centredLeft);

    // Update date text with current date
    auto now = juce::Time::getCurrentTime();
    dateLabel.setText(now.toString(true, false, false), juce::dontSendNotification); // Show date only

    addAndMakeVisible(videoSectionLabel);
    addAndMakeVisible(audioSectionLabel);
    addAndMakeVisible(audioPanel);
    addAndMakeVisible(videoPanel);
    addAndMakeVisible(appLogoComponent);
    addAndMakeVisible(dateLabel);  // Add date label
    addAndMakeVisible(presetsLabel);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(outputLabel);
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(hoursEditor);
    addAndMakeVisible(minutesEditor);
    addAndMakeVisible(secondsEditor);
    addAndMakeVisible(hoursLabel);
    addAndMakeVisible(minutesLabel);
    addAndMakeVisible(secondsLabel);
    addAndMakeVisible(fadeInLabel);
    addAndMakeVisible(fadeInEditor);
    addAndMakeVisible(fadeOutLabel);
    addAndMakeVisible(fadeOutEditor);
    addAndMakeVisible(renderButton);
    
    // Streaming controls
    addAndMakeVisible(streamingLabel);
    addAndMakeVisible(platformLabel);
    addAndMakeVisible(platformSelector);
    addAndMakeVisible(rtmpKeyLabel);
    addAndMakeVisible(rtmpKeyEditor);
    addAndMakeVisible(bitrateLabel);
    addAndMakeVisible(bitrateEditor);
    addAndMakeVisible(streamButton);
    
    addAndMakeVisible(progressMessages);
    addAndMakeVisible(masterMeter);
    addAndMakeVisible(binauralMeter);
    addAndMakeVisible(fileMeter);
    addAndMakeVisible(noiseMeter);
    
    // Stream health monitoring (initially hidden)
    addAndMakeVisible(healthBackgroundPanel);
    addAndMakeVisible(streamHealthLabel);
    addAndMakeVisible(bitrateActualLabel);
    addAndMakeVisible(fpsLabel);
    addAndMakeVisible(droppedFramesLabel);
    addAndMakeVisible(connectionStatusLabel);
    
    // Configure health background panel
    healthBackgroundPanel.setVisible(false);
    
    // Initially hide stream health elements
    streamHealthLabel.setVisible(false);
    bitrateActualLabel.setVisible(false);
    fpsLabel.setVisible(false);
    droppedFramesLabel.setVisible(false);
    connectionStatusLabel.setVisible(false);
    
    // Add button listeners
    renderButton.addListener(this);
    streamButton.addListener(this);
    saveButton.addListener(this);
    loadButton.addListener(this);
    playPauseButton.addListener(this);
    stopButton.addListener(this);
    
    // Initialize streaming controls
    platformSelector.addItem("YouTube", 1);
    platformSelector.addItem("Twitch", 2);
    platformSelector.addItem("Custom", 3);
    platformSelector.setSelectedId(1); // Default to YouTube
    
    rtmpKeyEditor.setMultiLine(false);
    rtmpKeyEditor.setReturnKeyStartsNewLine(false);
    rtmpKeyEditor.setTextToShowWhenEmpty("Enter your stream key here", juce::Colours::grey);
    
    bitrateEditor.setMultiLine(false);
    bitrateEditor.setReturnKeyStartsNewLine(false);
    bitrateEditor.setText("6000"); // Default 6Mbps
    bitrateEditor.setInputRestrictions(6, "0123456789"); // Numbers only
    
    // Initialize streamer
    streamer = std::make_unique<YoutubeStreamer>();
    streamer->onStatusUpdate = [this](const juce::String& message) {
        juce::MessageManager::callAsync([this, message]() {
            addProgressMessage(message);
        });
    };
    streamer->onError = [this](const juce::String& error) {
        juce::MessageManager::callAsync([this, error]() {
            addProgressMessage("ERROR: " + error);
            updateStreamingUI();
        });
    };
    
    streamer->onFFmpegOutput = [this](const juce::String& output) {
        parseFFmpegStats(output);
    };

    updateStreamingUI();
    startTimer(50);
    stopButton.setEnabled(false);

    if (binauralSource)
        binauralSource->setGain(0.0f);

    setAudioChannels(0, 2);
    setSize(1200, 800);
}

MainComponent::~MainComponent()
{
    if (streamer)
    {
        streamer->stopStreaming();
        juce::Thread::sleep(1000);
        streamer.reset();
    }

    if (renderingThread != nullptr)
        renderingThread->stopThread(2000);

    if (binauralSource)
        binauralSource->setGain(0.0f);
    if (filePlayer)
        filePlayer->stop();

    stopTimer();
    shutdownAudio();
    mixer.removeAllInputs();
}

void MainComponent::paint(juce::Graphics& g)
{
    // Simple gray background
    g.fillAll(juce::Colour(128, 128, 128)); // Medium gray
    
    auto r = getLocalBounds().reduced(4);
    int totalHeight = r.getHeight();
    
    // USE SAME PERCENTAGE CALCULATIONS AS RESIZED() METHOD
    const int MIN_TOOLBAR = 40;
    const int MIN_VIDEO = 150;
    const int MIN_AUDIO = 250;
    const int MIN_OUTPUT = 60;
    const int MIN_STREAMING = 60;
    const int MIN_HEALTH = 25;    // New: dedicated health monitoring line
    const int MIN_PROGRESS = 60;  // Reduced to make room for health line
    const int MIN_TOTAL = MIN_TOOLBAR + MIN_VIDEO + MIN_AUDIO + MIN_OUTPUT + MIN_STREAMING + MIN_HEALTH + MIN_PROGRESS;
    const int GAPS = 6 * 4; // 6 gaps of 4px each (added health section)
    
    double scaleFactor = (double)(totalHeight - GAPS) / MIN_TOTAL;
    if (scaleFactor > 1.0) scaleFactor = 1.0;
    
    // Calculate actual heights - EXACTLY matching resized() method
    int toolbarHeight = juce::jmax(MIN_TOOLBAR, (int)(totalHeight * 0.06));
    int videoHeight = juce::jmax((int)(MIN_VIDEO * scaleFactor), (int)(totalHeight * 0.25)); // 25% - back to original
    int audioHeight = juce::jmax((int)(MIN_AUDIO * scaleFactor), (int)(totalHeight * 0.35)); // 35%
    int outputHeight = juce::jmax((int)(MIN_OUTPUT * scaleFactor), (int)(totalHeight * 0.09)); // 9%
    int streamingHeight = juce::jmax((int)(MIN_STREAMING * scaleFactor), (int)(totalHeight * 0.09)); // 9%
    int healthHeight = juce::jmax((int)(MIN_HEALTH * scaleFactor), (int)(totalHeight * 0.04)); // 4%
    
    // Calculate Y positions for section boundaries
    auto toolbarArea = r.removeFromTop(toolbarHeight);
    r.removeFromTop(4);
    
    auto videoArea = r.removeFromTop(videoHeight);
    r.removeFromTop(4);
    
    auto audioArea = r.removeFromTop(audioHeight);
    r.removeFromTop(4);
    
    auto outputArea = r.removeFromTop(outputHeight);
    r.removeFromTop(4);
    
    auto streamingArea = r.removeFromTop(streamingHeight);
    r.removeFromTop(4);
    
    auto healthArea = r.removeFromTop(healthHeight);
    r.removeFromTop(4);
    
    // Draw section backgrounds with alternating lightness and darker headers
    
    // Toolbar section - lightest
    g.setColour(juce::Colour(160, 160, 160));
    g.fillRect(toolbarArea.expanded(4, 0).translated(-4, 0));
    
    // Video section - medium dark
    g.setColour(juce::Colour(120, 120, 120));
    g.fillRect(videoArea.expanded(4, 0).translated(-4, 0));
    // Video header (darker)
    g.setColour(juce::Colour(90, 90, 90));
    g.fillRect(videoArea.getX() - 4, videoArea.getY(), videoArea.getWidth() + 8, 25);
    
    // Audio section - light
    g.setColour(juce::Colour(150, 150, 150));
    g.fillRect(audioArea.expanded(4, 0).translated(-4, 0));
    // Audio header (darker)
    g.setColour(juce::Colour(110, 110, 110));
    g.fillRect(audioArea.getX() - 4, audioArea.getY(), audioArea.getWidth() + 8, 25);
    
    // Output section - medium dark
    g.setColour(juce::Colour(120, 120, 120));
    g.fillRect(outputArea.expanded(4, 0).translated(-4, 0));
    // Output header (darker)
    g.setColour(juce::Colour(90, 90, 90));
    g.fillRect(outputArea.getX() - 4, outputArea.getY(), outputArea.getWidth() + 8, 25);
    
    // Streaming section - light
    g.setColour(juce::Colour(150, 150, 150));
    g.fillRect(streamingArea.expanded(4, 0).translated(-4, 0));
    // Streaming header (darker)
    g.setColour(juce::Colour(110, 110, 110));
    g.fillRect(streamingArea.getX() - 4, streamingArea.getY(), streamingArea.getWidth() + 8, 25);
    
    // Health monitoring section - medium dark
    g.setColour(juce::Colour(115, 115, 115));
    g.fillRect(healthArea.expanded(4, 0).translated(-4, 0));
    
    // Progress section - darkest
    auto progressArea = r; // Remaining area
    g.setColour(juce::Colour(100, 100, 100));
    g.fillRect(progressArea.expanded(4, 0).translated(-4, 0));
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);
    streamingMixer.prepareToPlay(samplesPerBlockExpected, sampleRate);

    streamingMixer.addInputSource(streamingBinauralSource.get(), false);
    streamingMixer.addInputSource(streamingFilePlayer.get(), false);
    streamingMixer.addInputSource(streamingNoiseSource.get(), false);

    if (streamingBinauralSource)
        streamingBinauralSource->setGain(0.0f);

    if (streamingNoiseSource)
    {
        streamingNoiseSource->setMuted(true);
        streamingNoiseSource->setGain(0.0f);
    }

    if (streamingFilePlayer)
        streamingFilePlayer->start();

    updateStreamingAudioSettings();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    mixer.getNextAudioBlock(bufferToFill);

    if (bufferToFill.buffer && bufferToFill.numSamples > 0)
        applyLimiterToBuffer(*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples);

    float leftLevel = 0.0f;
    float rightLevel = 0.0f;

    if (bufferToFill.buffer && bufferToFill.numSamples > 0)
    {
        if (bufferToFill.buffer->getNumChannels() >= 1)
            leftLevel = bufferToFill.buffer->getRMSLevel(0, bufferToFill.startSample, bufferToFill.numSamples);

        if (bufferToFill.buffer->getNumChannels() >= 2)
            rightLevel = bufferToFill.buffer->getRMSLevel(1, bufferToFill.startSample, bufferToFill.numSamples);
        else
            rightLevel = leftLevel;
    }

    masterMeter.setLevels(leftLevel, rightLevel);

    if (binauralSource && filePlayer && noiseSource)
    {
        binauralMeter.setLevel(binauralSource->getGain() * leftLevel);
        fileMeter.setLevel(filePlayer->getGain() * rightLevel);
        noiseMeter.setLevel(noiseSource->getGain() * leftLevel);
    }

    if (isStreaming && streamer)
    {
        juce::AudioBuffer<float> streamBuffer(bufferToFill.buffer->getNumChannels(), bufferToFill.numSamples);
        juce::AudioSourceChannelInfo streamInfo(&streamBuffer, 0, bufferToFill.numSamples);

        streamingMixer.getNextAudioBlock(streamInfo);
        if (streamBuffer.getNumSamples() > 0)
            applyLimiterToBuffer(streamBuffer, 0, streamBuffer.getNumSamples());

        if (streamBuffer.getNumSamples() > 0)
        {
            const float* const* streamChannelData = streamBuffer.getArrayOfReadPointers();
            streamer->sendAudioData(streamChannelData, streamBuffer.getNumSamples(), streamBuffer.getNumChannels());
        }
    }
}

void MainComponent::releaseResources()
{
    mixer.releaseResources();
    streamingMixer.releaseResources();
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &renderButton)
    {
        startRendering();
    }
    else if (button == &saveButton)
    {
        saveProject();
    }
    else if (button == &loadButton)
    {
        loadProject();
    }
    else if (button == &playPauseButton)
    {
        if (transportState == Stopped || transportState == Paused)
        {
            transportState = Starting;
            updateTransportState();
        }
        else if (transportState == Playing)
        {
            transportState = Pausing;
            updateTransportState();
        }
    }
    else if (button == &stopButton)
    {
        if (transportState == Playing || transportState == Paused)
        {
            transportState = Stopping;
            updateTransportState();
        }
    }
    else if (button == &streamButton)
    {
        if (streamState == StreamIdle || streamState == StreamFailed)
            startStreaming();
        else if (streamState == StreamLive)
            stopStreaming();
    }
}

//======================================================================================
void MainComponent::resized()
{
    // Get bounds with minimal padding
    auto r = getLocalBounds().reduced(4);
    int totalHeight = r.getHeight();
    
    // PERCENTAGE-BASED LAYOUT CALCULATION
    // Minimum requirements vs available space
    const int MIN_TOOLBAR = 40;
    const int MIN_VIDEO = 150;
    const int MIN_AUDIO = 250;  // Reduced minimum for tighter faders
    const int MIN_OUTPUT = 60;
    const int MIN_STREAMING = 60;
    const int MIN_HEALTH = 25;    // New: dedicated health monitoring line
    const int MIN_PROGRESS = 60;  // Reduced to make room for health line
    const int MIN_TOTAL = MIN_TOOLBAR + MIN_VIDEO + MIN_AUDIO + MIN_OUTPUT + MIN_STREAMING + MIN_HEALTH + MIN_PROGRESS;
    const int GAPS = 6 * 4; // 6 gaps of 4px each (added health section)
    
    // Calculate scaling factor if window is too small
    double scaleFactor = (double)(totalHeight - GAPS) / MIN_TOTAL;
    if (scaleFactor > 1.0) scaleFactor = 1.0; // Don't scale up past minimums
    
    // Calculate actual heights with percentage distribution
    int toolbarHeight = juce::jmax(MIN_TOOLBAR, (int)(totalHeight * 0.06)); // 6% or minimum
    int videoHeight = juce::jmax((int)(MIN_VIDEO * scaleFactor), (int)(totalHeight * 0.25)); // 25% or scaled minimum
    int audioHeight = juce::jmax((int)(MIN_AUDIO * scaleFactor), (int)(totalHeight * 0.35)); // 35% or scaled minimum  
    int outputHeight = juce::jmax((int)(MIN_OUTPUT * scaleFactor), (int)(totalHeight * 0.09)); // 9% or scaled minimum
    int streamingHeight = juce::jmax((int)(MIN_STREAMING * scaleFactor), (int)(totalHeight * 0.09)); // 9% or scaled minimum
    int healthHeight = juce::jmax((int)(MIN_HEALTH * scaleFactor), (int)(totalHeight * 0.04)); // 4% or scaled minimum
    // Progress gets remaining space
    
    // Top toolbar
    auto toolbarArea = r.removeFromTop(toolbarHeight);
    
    // App logo - percentage of toolbar width from left edge
    int logoWidth = juce::jmin(350, (int)(toolbarArea.getWidth() * 0.4)); // 40% of width or 350px max
    appLogoComponent.setBounds(toolbarArea.getX() - 100, toolbarArea.getY(), logoWidth, toolbarHeight + 8); // Large negative offset to push to far left

    // Date label - positioned right after the logo
    int dateX = appLogoComponent.getRight() + 10;
    int dateWidth = 120;
    dateLabel.setBounds(dateX, toolbarArea.getY() + (toolbarHeight - 20) / 2, dateWidth, 20);

    // PRESETS section on the RIGHT - responsive button sizing
    int buttonWidth = juce::jmax(60, juce::jmin(80, (int)(toolbarArea.getWidth() / 15))); // Scale with window width
    int buttonHeight = toolbarHeight - 8; // Fit within toolbar
    int buttonSpacing = 6;
    
    auto rightEdge = toolbarArea.getRight() - 8;
    
    // LOAD button
    loadButton.setBounds(rightEdge - buttonWidth, 
                         toolbarArea.getCentreY() - buttonHeight/2, 
                         buttonWidth, buttonHeight);
    rightEdge -= (buttonWidth + buttonSpacing);
    
    // SAVE button
    saveButton.setBounds(rightEdge - buttonWidth, 
                        toolbarArea.getCentreY() - buttonHeight/2, 
                        buttonWidth, buttonHeight);
    rightEdge -= (buttonWidth + buttonSpacing);
    
    // PRESETS label
    int labelWidth = juce::jmax(60, buttonWidth);
    presetsLabel.setBounds(rightEdge - labelWidth, 
                          toolbarArea.getCentreY() - buttonHeight/2, 
                          labelWidth, buttonHeight);
    
    // Video section
    r.removeFromTop(4);
    auto videoArea = r.removeFromTop(videoHeight);
    
    int headerHeight = juce::jmin(24, (int)(videoHeight / 6)); // Scale header with section
    videoSectionLabel.setBounds(videoArea.getX() + 8, videoArea.getY() + 4, 100, headerHeight);
    videoPanel.setBounds(videoArea.reduced(8).withTrimmedTop(headerHeight + 4));
    
    // Audio section
    r.removeFromTop(4);
    auto audioArea = r.removeFromTop(audioHeight);
    
    headerHeight = juce::jmin(24, (int)(audioHeight / 10)); // Scale header
    audioSectionLabel.setBounds(audioArea.getX() + 8, audioArea.getY() + 4, 100, headerHeight);
    
    // Master meter - increase width and ensure visibility
    int meterWidth = juce::jmax(80, juce::jmin(120, (int)(audioArea.getWidth() / 15))); // Increased minimum from 40 to 80
    auto meterArea = audioArea.removeFromRight(meterWidth);
    masterMeter.setBounds(meterArea.reduced(2).withTrimmedTop(headerHeight + 4)); // Reduced padding from 4 to 2
    
    // Transport controls - scale with available space
    int transportButtonWidth = juce::jmax(50, juce::jmin(80, (int)(audioArea.getWidth() / 20)));
    int transportButtonHeight = juce::jmin(buttonHeight, headerHeight);
    
    auto transportX = getWidth() - (2 * transportButtonWidth + buttonSpacing) - 4; // Use window width minus 4px for edge padding
    auto transportY = audioArea.getY() + (headerHeight - transportButtonHeight) / 2; // Center in header
    
    playPauseButton.setBounds(transportX, transportY, transportButtonWidth, transportButtonHeight);
    stopButton.setBounds(transportX + transportButtonWidth + buttonSpacing, transportY, transportButtonWidth, transportButtonHeight);
    
    // Audio panel gets remaining space
    auto audioContentArea = audioArea.reduced(8).withTrimmedTop(headerHeight + 4);
    audioPanel.setBounds(audioContentArea);
    
    // Position external meters to the right of each track
    auto binauralMeterBounds = audioPanel.getBinauralMeterBounds();
    auto fileMeterBounds = audioPanel.getFileMeterBounds();
    auto noiseMeterBounds = audioPanel.getNoiseMeterBounds();
    
    // Convert to MainComponent coordinates
    auto audioPanelPos = audioPanel.getPosition();
    binauralMeterBounds = binauralMeterBounds.translated(audioPanelPos.getX(), audioPanelPos.getY());
    fileMeterBounds = fileMeterBounds.translated(audioPanelPos.getX(), audioPanelPos.getY());
    noiseMeterBounds = noiseMeterBounds.translated(audioPanelPos.getX(), audioPanelPos.getY());
    
    // Position meters same height as track cards
    binauralMeter.setBounds(binauralMeterBounds);
    fileMeter.setBounds(fileMeterBounds);
    noiseMeter.setBounds(noiseMeterBounds);
    
    
    // Output section
    r.removeFromTop(4);
    auto outputArea = r.removeFromTop(outputHeight);
    
    headerHeight = juce::jmin(20, (int)(outputHeight / 4));
    outputLabel.setBounds(outputArea.getX() + 8, outputArea.getY() + 4, 100, headerHeight);
    
    auto controlsArea = outputArea.reduced(8).withTrimmedTop(headerHeight + 4);
    
    // Responsive field sizing
    int renderButtonWidth = juce::jmax(80, juce::jmin(120, (int)(controlsArea.getWidth() / 8)));
    int availableWidth = controlsArea.getWidth() - renderButtonWidth - 32; // Extra margins
    int fieldWidth = availableWidth / 3;
    int editorHeight = juce::jmax(20, controlsArea.getHeight() - 20);
    int labelHeight = juce::jmin(16, editorHeight - 4);
    
    // Duration, Fade In, Fade Out fields
    int x = controlsArea.getX();
    int y = controlsArea.getY();
    
    for (int field = 0; field < 3; field++)
    {
        if (field == 0) // Duration field with HH:MM:SS
        {
            durationLabel.setBounds(x, y, fieldWidth, labelHeight);
            
            int timeFieldWidth = (fieldWidth - 40) / 3;
            int timeX = x + 4;
            
            hoursEditor.setBounds(timeX, y + labelHeight, timeFieldWidth, editorHeight);
            hoursLabel.setBounds(timeX + timeFieldWidth, y + labelHeight, 12, editorHeight);
            timeX += timeFieldWidth + 15;
            
            minutesEditor.setBounds(timeX, y + labelHeight, timeFieldWidth, editorHeight);
            minutesLabel.setBounds(timeX + timeFieldWidth, y + labelHeight, 12, editorHeight);
            timeX += timeFieldWidth + 15;
            
            secondsEditor.setBounds(timeX, y + labelHeight, timeFieldWidth, editorHeight);
            secondsLabel.setBounds(timeX + timeFieldWidth, y + labelHeight, 12, editorHeight);
        }
        else if (field == 1) // Fade In
        {
            fadeInLabel.setBounds(x, y, fieldWidth, labelHeight);
            fadeInEditor.setBounds(x, y + labelHeight, fieldWidth, editorHeight);
        }
        else // Fade Out
        {
            fadeOutLabel.setBounds(x, y, fieldWidth, labelHeight);
            fadeOutEditor.setBounds(x, y + labelHeight, fieldWidth, editorHeight);
        }
        
        x += fieldWidth + 8;
    }
    
    // Render button - fill output section body (excluding header)
    int renderButtonHeight = controlsArea.getHeight() - 4; // Body height minus small margin
    renderButton.setBounds(controlsArea.getRight() - renderButtonWidth, 
                         controlsArea.getY() + 2, 
                         renderButtonWidth, renderButtonHeight);
    
    // Streaming section
    r.removeFromTop(4);
    auto streamingArea = r.removeFromTop(streamingHeight);
    
    headerHeight = juce::jmin(20, (int)(streamingHeight / 4));
    streamingLabel.setBounds(streamingArea.getX() + 8, streamingArea.getY() + 4, 100, headerHeight);
    
    auto streamControlsArea = streamingArea.reduced(8).withTrimmedTop(headerHeight + 4);
    
    // Responsive streaming field sizing - same width as render button for alignment
    int streamButtonWidth = renderButtonWidth;
    int streamAvailableWidth = streamControlsArea.getWidth() - streamButtonWidth - 32;
    int streamFieldWidth = streamAvailableWidth / 3;
    
    x = streamControlsArea.getX();
    y = streamControlsArea.getY();
    editorHeight = juce::jmax(20, streamControlsArea.getHeight() - 20);
    labelHeight = juce::jmin(16, editorHeight - 4);
    
    // Platform field
    platformLabel.setBounds(x, y, streamFieldWidth, labelHeight);
    platformSelector.setBounds(x, y + labelHeight, streamFieldWidth, editorHeight);
    x += streamFieldWidth + 8;
    
    // RTMP Key field (wider)
    int keyFieldWidth = streamFieldWidth + streamFieldWidth / 2;
    rtmpKeyLabel.setBounds(x, y, keyFieldWidth, labelHeight);
    rtmpKeyEditor.setBounds(x, y + labelHeight, keyFieldWidth, editorHeight);
    x += keyFieldWidth + 8;
    
    // Bitrate field (remaining space)
    int bitrateFieldWidth = streamControlsArea.getRight() - x - streamButtonWidth - 8;
    bitrateLabel.setBounds(x, y, bitrateFieldWidth, labelHeight);
    bitrateEditor.setBounds(x, y + labelHeight, bitrateFieldWidth, editorHeight);
    
    // Stream button - full height of streaming area
    int streamButtonHeight = streamControlsArea.getHeight() - 4;
    streamButton.setBounds(streamControlsArea.getRight() - streamButtonWidth, 
                         streamControlsArea.getY() + 2, 
                         streamButtonWidth, streamButtonHeight);
    
    // Health monitoring
    r.removeFromTop(4);
    auto healthArea = r.removeFromTop(healthHeight);
    
    // Health monitoring - always visible but grayed out when not streaming
    int healthLabelWidth = healthArea.getWidth() / 4;
    int healthLabelHeight = healthArea.getHeight() - 4;
    int healthY = healthArea.getY() + 2;
    
    // Position background panel for health area
    healthBackgroundPanel.setBounds(healthArea.reduced(2));
    healthBackgroundPanel.setVisible(true); // Always visible now
    
    // Single row layout - all health stats on one line
    bitrateActualLabel.setBounds(healthArea.getX() + 8, healthY, 
                                healthLabelWidth * 2 - 8, healthLabelHeight);
    fpsLabel.setBounds(healthArea.getX() + healthLabelWidth * 2, healthY, 
                      healthLabelWidth - 4, healthLabelHeight);
    connectionStatusLabel.setBounds(healthArea.getX() + healthLabelWidth * 3, healthY, 
                                   healthLabelWidth - 4, healthLabelHeight);
    droppedFramesLabel.setBounds(healthArea.getX() + 8, healthY, 
                                healthLabelWidth - 8, healthLabelHeight);
    
    // Arrange horizontally: Bitrate | FPS | Status | Dropped
    bitrateActualLabel.setBounds(healthArea.getX() + 8, healthY, 
                                healthLabelWidth + 20, healthLabelHeight);
    fpsLabel.setBounds(healthArea.getX() + healthLabelWidth + 30, healthY, 
                      healthLabelWidth - 20, healthLabelHeight);
    connectionStatusLabel.setBounds(healthArea.getX() + healthLabelWidth * 2 + 15, healthY, 
                                   healthLabelWidth - 10, healthLabelHeight);
    droppedFramesLabel.setBounds(healthArea.getX() + healthLabelWidth * 3 + 10, healthY, 
                                healthLabelWidth - 10, healthLabelHeight);
    
    // Make all health components visible and set smaller font
    bitrateActualLabel.setVisible(true);
    fpsLabel.setVisible(true);
    droppedFramesLabel.setVisible(true);
    connectionStatusLabel.setVisible(true);
    streamHealthLabel.setVisible(false); // Don't need header
    
    // Set smaller font for health monitoring
    juce::Font healthFont(11.0f);
    bitrateActualLabel.setFont(healthFont);
    fpsLabel.setFont(healthFont);
    droppedFramesLabel.setFont(healthFont);
    connectionStatusLabel.setFont(healthFont);
    
    if (!isStreaming)
    {
        bitrateActualLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
        fpsLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
        droppedFramesLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
        connectionStatusLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);

        bitrateActualLabel.setText("Bitrate: --", juce::dontSendNotification);
        fpsLabel.setText("FPS: --", juce::dontSendNotification);
        droppedFramesLabel.setText("Dropped: --", juce::dontSendNotification);
        connectionStatusLabel.setText("Status: Idle", juce::dontSendNotification);
    }
    
    // Progress area
    r.removeFromTop(4);
    
    // Progress messages use all remaining space
    progressMessages.setBounds(r.reduced(8, 4));
    
    // Configure progress messages
    progressMessages.setMultiLine(true);
    progressMessages.setReturnKeyStartsNewLine(true);
    progressMessages.setReadOnly(true);
    progressMessages.setScrollbarsShown(true);
    progressMessages.setCaretVisible(false);
}

void MainComponent::updateTransportState()
{
    switch (transportState)
    {
        case Starting:
            mixer.addInputSource(binauralSource.get(), false);
            mixer.addInputSource(filePlayer.get(), false);
            mixer.addInputSource(noiseSource.get(), false);

            if (filePlayer)
                filePlayer->start();

            if (binauralSource && !audioPanel.getBinauralTrack()->isMuted())
                if (auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(audioPanel.getBinauralTrack()))
                    if (!binauralTrack->isMuted())
                        binauralSource->setGain(binauralTrack->getGain());

            transportState = Playing;
            stopButton.setEnabled(true);
            break;

        case Playing:
            break;

        case Pausing:
            transportState = Paused;
            break;

        case Paused:
            break;

        case Stopping:
            if (filePlayer)
                filePlayer->stop();

            if (binauralSource)
                binauralSource->setGain(0.0f);

            mixer.removeAllInputs();
            transportState = Stopped;
            stopButton.setEnabled(false);
            break;

        case Stopped:
            break;
    }
}

void MainComponent::saveProject()
{
    // Create a file chooser and keep it alive
    saveProjectChooser.reset(new juce::FileChooser("Save Project",
                             juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                             "*.ambient"));
    
    // Use async method and keep object alive with class member
    saveProjectChooser->launchAsync(juce::FileBrowserComponent::saveMode | 
                          juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser)
    {
        juce::File file = chooser.getResult();
        if (file != juce::File{})
        {
            juce::ValueTree project("AmbientProject");
            
            // Add project properties - separate hour, minute, second values
            project.setProperty("durationHours", hoursEditor.getText(), nullptr);
            project.setProperty("durationMinutes", minutesEditor.getText(), nullptr);
            project.setProperty("durationSeconds", secondsEditor.getText(), nullptr);
            
            // Also save total duration in seconds for backward compatibility
            int hours = hoursEditor.getText().getIntValue();
            int minutes = minutesEditor.getText().getIntValue();
            int seconds = secondsEditor.getText().getIntValue();
            double totalDuration = (hours * 3600) + (minutes * 60) + seconds;
            project.setProperty("duration", juce::String(totalDuration), nullptr);
            
            project.setProperty("fadeIn", fadeInEditor.getText(), nullptr);
            project.setProperty("fadeOut", fadeOutEditor.getText(), nullptr);
            
            // Save all audio settings from the UI
            auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(audioPanel.getBinauralTrack());
            auto* fileTrack = dynamic_cast<FileTrackComponent*>(audioPanel.getFileTrack());
            auto* noiseTrack = dynamic_cast<NoiseTrackComponent*>(audioPanel.getNoiseTrack());
            
            juce::ValueTree audioSettings("AudioSettings");
            
            if (binauralTrack)
            {
                juce::ValueTree binauralData("BinauralTrack");
                binauralData.setProperty("leftFrequency", binauralTrack->getLeftFrequency(), nullptr);
                binauralData.setProperty("rightFrequency", binauralTrack->getRightFrequency(), nullptr);
                binauralData.setProperty("gainValue", (float)binauralTrack->getGain(), nullptr);
                binauralData.setProperty("muted", (bool)binauralTrack->isMuted(), nullptr);
                binauralData.setProperty("solo", (bool)binauralTrack->isSolo(), nullptr);
                // binauralData.setProperty("autoPlay", binauralTrack->shouldAutoPlay(), nullptr);
                audioSettings.addChild(binauralData, -1, nullptr);
            }
            
            // Save audio file track settings
            if (fileTrack)
            {
                juce::ValueTree fileData("FileTrack");
                fileData.setProperty("gainValue", (float)fileTrack->getGain(), nullptr);
                fileData.setProperty("muted", (bool)fileTrack->isMuted(), nullptr);
                fileData.setProperty("solo", (bool)fileTrack->isSolo(), nullptr);

                juce::ValueTree playlistTree("Playlist");
                auto playlistItems = fileTrack->getPlaylistItems();
                if (!playlistItems.empty())
                {
                    fileData.setProperty("filePath", playlistItems.front().file.getFullPathName(), nullptr);

                    for (const auto& item : playlistItems)
                    {
                        juce::ValueTree entry("Item");
                        entry.setProperty("type", item.type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile ? "audio" : "silence", nullptr);
                        entry.setProperty("filePath", item.file.getFullPathName(), nullptr);
                        entry.setProperty("displayName", item.displayName, nullptr);
                        entry.setProperty("targetDuration", item.targetDurationSeconds, nullptr);
                        entry.setProperty("repetitions", item.repetitions, nullptr);
                        entry.setProperty("crossfade", item.crossfadeSeconds, nullptr);
                        playlistTree.addChild(entry, -1, nullptr);
                    }
                    fileData.addChild(playlistTree, -1, nullptr);
                }
                else
                {
                    fileData.setProperty("filePath", fileTrack->getLoadedFilePath(), nullptr);
                }

                audioSettings.addChild(fileData, -1, nullptr);
            }
            
            // Save noise track settings
            if (noiseTrack)
            {
                juce::ValueTree noiseData("NoiseTrack");
                noiseData.setProperty("noiseType", (int)audioPanel.getNoiseSource()->getNoiseType(), nullptr);
                noiseData.setProperty("gainValue", (float)noiseTrack->getGain(), nullptr);
                noiseData.setProperty("muted", (bool)noiseTrack->isMuted(), nullptr);
                noiseData.setProperty("solo", (bool)noiseTrack->isSolo(), nullptr);
                audioSettings.addChild(noiseData, -1, nullptr);
            }
            
            project.addChild(audioSettings, -1, nullptr);
            
            // Save video clips
            juce::ValueTree videoClips("VideoClips");
            
            // Add intro clips
            juce::ValueTree introClips("IntroClips");
            for (int i = 0; i < videoPanel.getNumIntroClips(); ++i)
            {
                auto clipData = videoPanel.getIntroClipData(i);
                
                juce::ValueTree clip("Clip");
                clip.setProperty("filePath", clipData.filePath, nullptr);
                clip.setProperty("duration", clipData.duration, nullptr);
                clip.setProperty("crossfade", clipData.crossfade, nullptr);
                introClips.addChild(clip, -1, nullptr);
            }
            videoClips.addChild(introClips, -1, nullptr);
            
            // Add loop clips
            juce::ValueTree loopClips("LoopClips");
            for (int i = 0; i < videoPanel.getNumLoopClips(); ++i)
            {
                auto clipData = videoPanel.getLoopClipData(i);
                
                juce::ValueTree clip("Clip");
                clip.setProperty("filePath", clipData.filePath, nullptr);
                clip.setProperty("duration", clipData.duration, nullptr);
                clip.setProperty("crossfade", clipData.crossfade, nullptr);
                loopClips.addChild(clip, -1, nullptr);
            }
            videoClips.addChild(loopClips, -1, nullptr);
            
            // Add overlay clips
            juce::ValueTree overlayClips("OverlayClips");
            for (int i = 0; i < videoPanel.getNumOverlayClips(); ++i)
            {
                auto clipData = videoPanel.getOverlayClipData(i);
                
                juce::ValueTree clip("Clip");
                clip.setProperty("filePath", clipData.filePath, nullptr);
                clip.setProperty("duration", clipData.duration, nullptr);
                clip.setProperty("frequencySecs", clipData.frequencySecs, nullptr);
                clip.setProperty("startTimeSecs", clipData.startTimeSecs, nullptr);
                overlayClips.addChild(clip, -1, nullptr);
            }
            videoClips.addChild(overlayClips, -1, nullptr);
            
            project.addChild(videoClips, -1, nullptr);
            
            // Write to file using FileOutputStream
            std::unique_ptr<juce::FileOutputStream> outputStream(file.createOutputStream());
            if (outputStream != nullptr)
            {
                project.writeToStream(*outputStream);
                progressMessages.moveCaretToEnd();
                progressMessages.insertTextAtCaret("Project saved to: " + file.getFullPathName() + "\n");
            }
        }
    });
}

void MainComponent::loadProject()
{
    // Create a file chooser and keep it alive
    loadProjectChooser.reset(new juce::FileChooser("Load Project",
                             juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                             "*.ambient"));
    
    // Use async method and keep object alive with class member
    loadProjectChooser->launchAsync(juce::FileBrowserComponent::openMode | 
                          juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser)
    {
        juce::File file = chooser.getResult();
        if (file.existsAsFile())
        {
            // Log file loading attempt
            progressMessages.moveCaretToEnd();
            progressMessages.insertTextAtCaret("Attempting to load project from: " + file.getFullPathName() + "\n");
            
            // Read directly as a ValueTree, not as XML
            progressMessages.moveCaretToEnd();
            progressMessages.insertTextAtCaret("Reading file as binary valueTree\n");
            
            // Try to load as a binary file first
            juce::ValueTree project;
            
            // The file might be in binary format (older ambient projects)
            juce::FileInputStream inputStream(file);
            if (inputStream.openedOk())
            {
                // Try to read as binary first
                project = juce::ValueTree::readFromStream(inputStream);
                
                if (!project.isValid())
                {
                    // If binary read fails, try XML
                    inputStream.setPosition(0); // Reset to beginning of file
                    juce::String xmlContent = inputStream.readString();
                    progressMessages.moveCaretToEnd();
                    progressMessages.insertTextAtCaret("Binary read failed, trying XML: " + juce::String(xmlContent.length()) + " bytes\n");
                    
                    project = juce::ValueTree::fromXml(xmlContent);
                }
                
                // Check if project loaded correctly
                if (!project.isValid())
                {
                    progressMessages.moveCaretToEnd();
                    progressMessages.insertTextAtCaret("Error: Failed to parse project file. Invalid XML format.\n");
                    return;
                }
                
                // Verify project type
                progressMessages.moveCaretToEnd();
                progressMessages.insertTextAtCaret("Project type: " + project.getType().toString() + "\n");
                
                if (project.isValid() && project.hasType("AmbientProject"))
                {
                    // Clear existing data
                    videoPanel.clearAllClips();
                    
                    // Stop any playing audio to avoid crashes when loading new audio
                    if (filePlayer)
                        filePlayer->stop();
                    
                    // Always stop binaural audio when loading a project
                    if (binauralSource)
                        binauralSource->setGain(0.0f);
                        
                    transportState = Stopped;
                    // Keep play button (it shows play icon)
                    stopButton.setEnabled(false);
                    
                    // Load project properties for hours, minutes, seconds fields
                    if (project.hasProperty("durationHours"))
                        hoursEditor.setText(project.getProperty("durationHours").toString());
                    else
                        hoursEditor.setText("0");
                    
                    if (project.hasProperty("durationMinutes"))
                        minutesEditor.setText(project.getProperty("durationMinutes").toString());
                    else
                        minutesEditor.setText("0");
                    
                    if (project.hasProperty("durationSeconds"))
                        secondsEditor.setText(project.getProperty("durationSeconds").toString());
                    else
                        secondsEditor.setText("0");
                        
                    // Fallback to legacy duration format if new fields aren't present
                    if (!project.hasProperty("durationHours") && 
                        !project.hasProperty("durationMinutes") && 
                        !project.hasProperty("durationSeconds") && 
                        project.hasProperty("duration"))
                    {
                        // Parse the total duration back into hours, minutes, seconds
                        double totalDuration = project.getProperty("duration").toString().getDoubleValue();
                        int hours = static_cast<int>(totalDuration) / 3600;
                        int minutes = (static_cast<int>(totalDuration) % 3600) / 60;
                        int seconds = static_cast<int>(totalDuration) % 60;
                        
                        // Set the separate editor fields
                        hoursEditor.setText(juce::String(hours));
                        minutesEditor.setText(juce::String(minutes));
                        secondsEditor.setText(juce::String(seconds));
                        
                        progressMessages.moveCaretToEnd();
                        progressMessages.insertTextAtCaret("Converted legacy duration format to HH:MM:SS\n");
                    }
                    
                    if (project.hasProperty("fadeIn"))
                        fadeInEditor.setText(project.getProperty("fadeIn").toString());
                    
                    if (project.hasProperty("fadeOut"))
                        fadeOutEditor.setText(project.getProperty("fadeOut").toString());
                    
                    // Load audio settings
                    juce::ValueTree audioSettings = project.getChildWithName("AudioSettings");
                    if (audioSettings.isValid())
                    {
                        // Load binaural track settings
                        juce::ValueTree binauralData = audioSettings.getChildWithName("BinauralTrack");
                        if (binauralData.isValid())
                        {
                            auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(audioPanel.getBinauralTrack());
                            if (binauralTrack)
                            {
                                if (binauralData.hasProperty("leftFrequency"))
                                    binauralTrack->setLeftFrequency(binauralData.getProperty("leftFrequency"));
                                
                                if (binauralData.hasProperty("rightFrequency"))
                                    binauralTrack->setRightFrequency(binauralData.getProperty("rightFrequency"));
                                
                                if (binauralData.hasProperty("gainValue"))
                                    binauralTrack->setGain((float)binauralData.getProperty("gainValue"));
                                
                                if (binauralData.hasProperty("muted"))
                                    binauralTrack->setMuteState((bool)binauralData.getProperty("muted"));
                                
                                if (binauralData.hasProperty("solo"))
                                    binauralTrack->setSoloState((bool)binauralData.getProperty("solo"));
                                
                                // if (binauralData.hasProperty("autoPlay"))
                                //     binauralTrack->setShouldAutoPlay((bool)binauralData.getProperty("autoPlay"));
                            }
                        }
                        
                        // Load file track settings
                        juce::ValueTree fileData = audioSettings.getChildWithName("FileTrack");
                        if (fileData.isValid())
                        {
                            auto* fileTrack = dynamic_cast<FileTrackComponent*>(audioPanel.getFileTrack());
                            if (fileTrack)
                            {
                                std::vector<FilePlayerAudioSource::PlaylistItem> playlistItems;
                                juce::ValueTree playlistTree = fileData.getChildWithName("Playlist");
                                if (playlistTree.isValid())
                                {
                                    for (int i = 0; i < playlistTree.getNumChildren(); ++i)
                                    {
                                        auto entry = playlistTree.getChild(i);
                                        FilePlayerAudioSource::PlaylistItem item;
                                        juce::String type = entry.getProperty("type").toString();
                                        if (type == "silence")
                                        {
                                            item = FilePlayerAudioSource::PlaylistItem::createSilence(
                                                (double)entry.getProperty("targetDuration"),
                                                (double)entry.getProperty("crossfade"));
                                        }
                                        else
                                        {
                                            juce::File audioFile(entry.getProperty("filePath").toString());
                                            item = FilePlayerAudioSource::PlaylistItem::createAudio(
                                                audioFile,
                                                (int)entry.getProperty("repetitions"),
                                                (double)entry.getProperty("targetDuration"),
                                                (double)entry.getProperty("crossfade"),
                                                entry.getProperty("displayName").toString());
                                        }
                                        playlistItems.push_back(item);
                                    }
                                    fileTrack->setPlaylistItems(playlistItems);
                                }
                                else if (fileData.hasProperty("filePath"))
                                {
                                    juce::File audioFile(fileData.getProperty("filePath").toString());
                                    if (audioFile.existsAsFile())
                                    {
                                        fileTrack->loadAudioFile(audioFile);
                                    }
                                    else
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Warning: Audio file not found: " + audioFile.getFullPathName() + "\n");
                                    }
                                }
                                
                                if (fileData.hasProperty("gainValue"))
                                    fileTrack->setGain((float)fileData.getProperty("gainValue"));
                                
                                if (fileData.hasProperty("muted"))
                                    fileTrack->setMuteState((bool)fileData.getProperty("muted"));
                                
                                if (fileData.hasProperty("solo"))
                                    fileTrack->setSoloState((bool)fileData.getProperty("solo"));
                            }
                        }
                        
                        // Load noise track settings
                        juce::ValueTree noiseData = audioSettings.getChildWithName("NoiseTrack");
                        if (noiseData.isValid())
                        {
                            auto* noiseTrack = dynamic_cast<NoiseTrackComponent*>(audioPanel.getNoiseTrack());
                            if (noiseTrack)
                            {
                                if (noiseData.hasProperty("noiseType"))
                                {
                                    auto noiseType = (NoiseAudioSource::NoiseType)(int)noiseData.getProperty("noiseType");
                                    noiseTrack->setNoiseType(noiseType);
                                }
                                
                                if (noiseData.hasProperty("gainValue"))
                                    noiseTrack->setGain((float)noiseData.getProperty("gainValue"));
                                
                                if (noiseData.hasProperty("muted"))
                                    noiseTrack->setMuteState((bool)noiseData.getProperty("muted"));
                                
                                if (noiseData.hasProperty("solo"))
                                    noiseTrack->setSoloState((bool)noiseData.getProperty("solo"));
                            }
                        }
                    }
                    
                    // For backwards compatibility
                    if (!audioSettings.isValid() || (!audioSettings.getChildWithName("BinauralTrack").isValid() && !audioSettings.getChildWithName("FileTrack").isValid()))
                    {
                        // Load binaural settings from old format
                        juce::ValueTree binauralSettings = project.getChildWithName("BinauralSettings");
                        if (binauralSettings.isValid())
                        {
                            auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(audioPanel.getBinauralTrack());
                            if (binauralTrack)
                            {
                                if (binauralSettings.hasProperty("leftFrequency"))
                                    binauralTrack->setLeftFrequency(binauralSettings.getProperty("leftFrequency"));
                                
                                if (binauralSettings.hasProperty("rightFrequency"))
                                    binauralTrack->setRightFrequency(binauralSettings.getProperty("rightFrequency"));
                            }
                        }
                            
                        // Load audio file from old format
                        if (audioSettings.isValid() && audioSettings.hasProperty("filePath"))
                        {
                            juce::String audioPath = audioSettings.getProperty("filePath").toString();
                            juce::File audioFile(audioPath);
                            
                            if (audioFile.existsAsFile())
                            {
                                auto* fileTrack = dynamic_cast<FileTrackComponent*>(audioPanel.getFileTrack());
                                if (fileTrack)
                                {
                                    fileTrack->loadAudioFile(audioFile);
                                }
                            }
                            else
                            {
                                progressMessages.moveCaretToEnd();
                                progressMessages.insertTextAtCaret("Warning: Audio file not found: " + audioPath + "\n");
                            }
                        }
                    }
                    
                    // Load video clips
                    juce::ValueTree videoClips = project.getChildWithName("VideoClips");
                    progressMessages.moveCaretToEnd();
                    progressMessages.insertTextAtCaret("Loading video clips section: " + juce::String(videoClips.isValid() ? "found" : "not found") + "\n");
                    
                    if (videoClips.isValid())
                    {
                        // Load intro clips
                        juce::ValueTree introClips = videoClips.getChildWithName("IntroClips");
                        progressMessages.moveCaretToEnd();
                        progressMessages.insertTextAtCaret("Loading intro clips section: " + juce::String(introClips.isValid() ? "found" : "not found") + "\n");
                        
                        if (introClips.isValid())
                        {
                            int numClips = introClips.getNumChildren();
                            progressMessages.moveCaretToEnd();
                            progressMessages.insertTextAtCaret("Found " + juce::String(numClips) + " intro clips\n");
                            
                            for (int i = 0; i < numClips; ++i)
                            {
                                juce::ValueTree clip = introClips.getChild(i);
                                if (clip.hasProperty("filePath"))
                                {
                                    juce::String filePath = clip.getProperty("filePath").toString();
                                    juce::File videoFile(filePath);
                                    
                                    double duration = 5.0;
                                    if (clip.hasProperty("duration"))
                                        duration = clip.getProperty("duration");
                                        
                                    double crossfade = 1.0;
                                    if (clip.hasProperty("crossfade"))
                                        crossfade = clip.getProperty("crossfade");
                                    
                                    if (videoFile.existsAsFile())
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Adding intro clip: " + filePath + " (duration: " + juce::String(duration) + ")\n");
                                        videoPanel.addIntroClip(videoFile, duration, crossfade);
                                    }
                                    else
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Warning: Video file not found: " + filePath + "\n");
                                    }
                                }
                            }
                        }
                        
                        // Load loop clips
                        juce::ValueTree loopClips = videoClips.getChildWithName("LoopClips");
                        progressMessages.moveCaretToEnd();
                        progressMessages.insertTextAtCaret("Loading loop clips section: " + juce::String(loopClips.isValid() ? "found" : "not found") + "\n");
                        
                        if (loopClips.isValid())
                        {
                            int numClips = loopClips.getNumChildren();
                            progressMessages.moveCaretToEnd();
                            progressMessages.insertTextAtCaret("Found " + juce::String(numClips) + " loop clips\n");
                            
                            for (int i = 0; i < numClips; ++i)
                            {
                                juce::ValueTree clip = loopClips.getChild(i);
                                if (clip.hasProperty("filePath"))
                                {
                                    juce::String filePath = clip.getProperty("filePath").toString();
                                    juce::File videoFile(filePath);
                                    
                                    double duration = 5.0;
                                    if (clip.hasProperty("duration"))
                                        duration = clip.getProperty("duration");
                                        
                                    double crossfade = 1.0;
                                    if (clip.hasProperty("crossfade"))
                                        crossfade = clip.getProperty("crossfade");
                                    
                                    if (videoFile.existsAsFile())
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Adding loop clip: " + filePath + " (duration: " + juce::String(duration) + ")\n");
                                        videoPanel.addLoopClip(videoFile, duration, crossfade);
                                    }
                                    else
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Warning: Video file not found: " + filePath + "\n");
                                    }
                                }
                            }
                        }
                        
                        // Load overlay clips
                        juce::ValueTree overlayClips = videoClips.getChildWithName("OverlayClips");
                        progressMessages.moveCaretToEnd();
                        progressMessages.insertTextAtCaret("Loading overlay clips section: " + juce::String(overlayClips.isValid() ? "found" : "not found") + "\n");
                        
                        if (overlayClips.isValid())
                        {
                            int numClips = overlayClips.getNumChildren();
                            progressMessages.moveCaretToEnd();
                            progressMessages.insertTextAtCaret("Found " + juce::String(numClips) + " overlay clips\n");
                            
                            for (int i = 0; i < numClips; ++i)
                            {
                                juce::ValueTree clip = overlayClips.getChild(i);
                                if (clip.hasProperty("filePath"))
                                {
                                    juce::String filePath = clip.getProperty("filePath").toString();
                                    juce::File videoFile(filePath);
                                    
                                    double duration = 5.0;
                                    if (clip.hasProperty("duration"))
                                        duration = clip.getProperty("duration");
                                        
                                    double frequencySecs = 5.0;
                                    if (clip.hasProperty("frequencySecs"))
                                        frequencySecs = clip.getProperty("frequencySecs");
                                    else if (clip.hasProperty("frequencyMin")) // Backward compatibility
                                        frequencySecs = clip.getProperty("frequencyMin");
                                    
                                    double startTimeSecs = 10.0; // Default start time
                                    if (clip.hasProperty("startTimeSecs"))
                                        startTimeSecs = static_cast<double>(clip.getProperty("startTimeSecs"));
                                    else if (clip.hasProperty("startWithOverlay")) 
                                        // For backwards compatibility with old projects
                                        startTimeSecs = static_cast<bool>(clip.getProperty("startWithOverlay")) ? 0.0 : 5.0;
                                    
                                    if (videoFile.existsAsFile())
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Adding overlay clip: " + filePath + " (duration: " + juce::String(duration) + ")\n");
                                        videoPanel.addOverlayClip(videoFile, duration, frequencySecs, startTimeSecs);
                                    }
                                    else
                                    {
                                        progressMessages.moveCaretToEnd();
                                        progressMessages.insertTextAtCaret("Warning: Video file not found: " + filePath + "\n");
                                    }
                                }
                            }
                        }
                        
                        progressMessages.moveCaretToEnd();
                        progressMessages.insertTextAtCaret("Project loaded from: " + file.getFullPathName() + "\n");
                        progressMessages.moveCaretToEnd();
                        
                        // Print a summary of what was loaded
                        progressMessages.insertTextAtCaret("\nProject Summary:\n");
                        progressMessages.insertTextAtCaret("- Duration: " + 
                                                       hoursEditor.getText() + "h " + 
                                                       minutesEditor.getText() + "m " + 
                                                       secondsEditor.getText() + "s\n");
                        progressMessages.insertTextAtCaret("- Fade In: " + fadeInEditor.getText() + " seconds\n");
                        progressMessages.insertTextAtCaret("- Fade Out: " + fadeOutEditor.getText() + " seconds\n");
                        progressMessages.insertTextAtCaret("- Intro Clips: " + juce::String(videoPanel.getNumIntroClips()) + "\n");
                        progressMessages.insertTextAtCaret("- Loop Clips: " + juce::String(videoPanel.getNumLoopClips()) + "\n");
                        progressMessages.insertTextAtCaret("- Overlay Clips: " + juce::String(videoPanel.getNumOverlayClips()) + "\n");
                    }
                }
            }
        }
    });
}

void MainComponent::startRendering()
{
    if (!isRendering)
    {
        // Stop any ongoing playback before rendering
        if (transportState == Playing || transportState == Paused)
        {
            transportState = Stopping;
            updateTransportState();
            juce::Thread::sleep(100);
        }

        mixer.removeAllInputs();

        if (filePlayer)
        {
            filePlayer->stop();
            filePlayer->setPosition(0.0);
        }

        // Note: Don't modify binaural gain here - RenderManager reads UI values directly

        progressMessages.moveCaretToEnd();
        progressMessages.insertTextAtCaret("Stopping playback and clearing audio buffers before rendering...\n");

        if (renderTimerOverlay != nullptr)
        {
            renderTimerOverlay->setVisible(false);
            renderTimerOverlay.reset();
        }

        // Convert video clips for rendering
        std::vector<RenderManager::VideoClipInfo> introClips;
        std::vector<RenderManager::VideoClipInfo> loopClips;

        for (int i = 0; i < videoPanel.getNumIntroClips(); i++)
        {
            auto clipData = videoPanel.getIntroClipData(i);
            RenderManager::VideoClipInfo info;
            info.file = juce::File::getCurrentWorkingDirectory().getChildFile(clipData.filePath);
            info.duration = clipData.duration;
            info.crossfade = clipData.crossfade;
            info.isIntroClip = true;
            introClips.push_back(info);
        }

        for (int i = 0; i < videoPanel.getNumLoopClips(); i++)
        {
            auto clipData = videoPanel.getLoopClipData(i);
            RenderManager::VideoClipInfo info;
            info.file = juce::File::getCurrentWorkingDirectory().getChildFile(clipData.filePath);
            info.duration = clipData.duration;
            info.crossfade = clipData.crossfade;
            info.isIntroClip = false;
            loopClips.push_back(info);
        }
        
        // Get output settings from the separate HH:MM:SS fields
        
        // Calculate duration in seconds from the separate fields
        int hours = hoursEditor.getText().getIntValue();
        int minutes = minutesEditor.getText().getIntValue();
        int seconds = secondsEditor.getText().getIntValue();
        
        // Validate and enforce limits on time values
        hours = juce::jlimit(0, 99, hours);     // Max 99 hours
        minutes = juce::jlimit(0, 59, minutes); // Max 59 minutes
        seconds = juce::jlimit(0, 59, seconds); // Max 59 seconds
        
        // Calculate total seconds
        double duration = (hours * 3600) + (minutes * 60) + seconds;
        
        // Log the calculated duration
        progressMessages.moveCaretToEnd();
        progressMessages.insertTextAtCaret("Using duration: " + 
                                      juce::String(hours) + "h " + 
                                      juce::String(minutes) + "m " + 
                                      juce::String(seconds) + "s = " + 
                                      juce::String(duration) + " seconds\n");
        
        // Parse fade in/out the same way
        double fadeIn = 0.0;
        juce::String fadeInText = fadeInEditor.getText();
        if (fadeInText.contains(":"))
        {
            juce::StringArray parts;
            parts.addTokens(fadeInText, ":", "");
            
            if (parts.size() == 3) // HH:MM:SS
            {
                fadeIn = (parts[0].getDoubleValue() * 3600) + (parts[1].getDoubleValue() * 60) + parts[2].getDoubleValue();
            }
            else if (parts.size() == 2) // MM:SS
            {
                fadeIn = (parts[0].getDoubleValue() * 60) + parts[1].getDoubleValue();
            }
            else
            {
                fadeIn = fadeInText.getDoubleValue();
            }
        }
        else
        {
            fadeIn = fadeInText.getDoubleValue();
        }
        
        double fadeOut = 0.0;
        juce::String fadeOutText = fadeOutEditor.getText();
        if (fadeOutText.contains(":"))
        {
            juce::StringArray parts;
            parts.addTokens(fadeOutText, ":", "");
            
            if (parts.size() == 3) // HH:MM:SS
            {
                fadeOut = (parts[0].getDoubleValue() * 3600) + (parts[1].getDoubleValue() * 60) + parts[2].getDoubleValue();
            }
            else if (parts.size() == 2) // MM:SS
            {
                fadeOut = (parts[0].getDoubleValue() * 60) + parts[1].getDoubleValue();
            }
            else
            {
                fadeOut = fadeOutText.getDoubleValue();
            }
        }
        else
        {
            fadeOut = fadeOutText.getDoubleValue();
        }
        
        // No debug file needed anymore - we figured out the correct implementation file
        
        // Create and populate overlays list
        std::vector<RenderManager::OverlayClipInfo> overlayClips;
        
        // Convert overlay clips
        for (int i = 0; i < videoPanel.getNumOverlayClips(); i++)
        {
            auto clipData = videoPanel.getOverlayClipData(i);
            RenderManager::OverlayClipInfo info;
            info.file = juce::File(clipData.filePath);
            info.duration = clipData.duration;
            info.frequencySecs = clipData.frequencySecs;
            info.startTimeSecs = clipData.startTimeSecs;
            overlayClips.push_back(info);
        }
        
        // Create the render dialog directly
        auto renderDialog = new RenderDialog(
            binauralSource.get(),
            filePlayer.get(),
            noiseSource.get(),
            introClips,
            loopClips,
            overlayClips,
            duration,
            fadeIn,
            fadeOut
        );
        
        // Log progress to our output window as well
        progressMessages.clear();
        progressMessages.moveCaretToEnd();
        progressMessages.insertTextAtCaret("Opening render dialog...\n");
        
        // Show dialog - the dialog will handle the audio-only option
        renderDialog->showDialog(this);
    }
}

void MainComponent::timerCallback()
{
    // Update the date label periodically (in case the date changes while app is running)
    static int timerCount = 0;
    timerCount++;

    // Update date every 1000 timer calls (approximately every 50 seconds at 50ms timer interval)
    if (timerCount >= 1000)
    {
        timerCount = 0;
        auto now = juce::Time::getCurrentTime();
        dateLabel.setText(now.toString(true, false, false), juce::dontSendNotification); // Show date only
    }
}

//==============================================================================
// STREAMING METHODS
//==============================================================================

void MainComponent::startStreaming()
{
    // Set preparing state
    streamState = StreamPreparing;
    updateStreamingUI();
    
    // Validate inputs
    juce::String rtmpKey = rtmpKeyEditor.getText().trim();
    if (rtmpKey.isEmpty())
    {
        progressMessages.insertTextAtCaret("ERROR: Please enter an RTMP stream key\n");
        progressMessages.moveCaretToEnd();
        streamState = StreamFailed;
        updateStreamingUI();
        return;
    }
    
    // Get platform-specific URL
    juce::String rtmpUrl;
    int platformId = platformSelector.getSelectedId();
    switch (platformId)
    {
        case 1: // YouTube
            rtmpUrl = "rtmp://a.rtmp.youtube.com/live2/";
            break;
        case 2: // Twitch
            rtmpUrl = "rtmp://live.twitch.tv/app/";
            break;
        case 3: // Custom
            rtmpUrl = rtmpKey; // For custom, the "key" is actually the full RTMP URL
            rtmpKey = ""; // No separate key for custom URLs
            break;
        default:
            progressMessages.insertTextAtCaret("ERROR: Please select a streaming platform\n");
            progressMessages.moveCaretToEnd();
            return;
    }
    
    // Get clip data from video panel (already converted to RenderTypes)
    auto introClips = videoPanel.getIntroClipsForStreaming();
    auto loopClips = videoPanel.getLoopClipsForStreaming();
    auto overlayClips = videoPanel.getOverlayClipsForStreaming();
    
    if (introClips.size() == 0 && loopClips.size() == 0)
    {
        progressMessages.insertTextAtCaret("ERROR: Please add some video clips before streaming\n");
        progressMessages.moveCaretToEnd();
        return;
    }
    
    // Clips are already converted to RenderTypes by the VideoPanel methods
    streamer->setSequence(introClips, loopClips, overlayClips);
    
    // Update streaming audio settings to match current UI state
    updateStreamingAudioSettings();
    
    // Get bitrate from UI
    int bitrate = bitrateEditor.getText().getIntValue();
    if (bitrate < 500) bitrate = 500;    // Minimum 500 kbps
    if (bitrate > 50000) bitrate = 50000; // Maximum 50 Mbps
    
    // Start streaming
    progressMessages.insertTextAtCaret("Starting infinite stream at " + juce::String(bitrate) + " kbps...\n");
    progressMessages.moveCaretToEnd();
    
    if (streamer->startStreaming(platformId == 3 ? rtmpUrl : rtmpKey, platformId, bitrate))
    {
        isStreaming = true;
        streamState = StreamLive;
        updateStreamingUI();
        progressMessages.insertTextAtCaret("Stream started successfully!\n");
        progressMessages.moveCaretToEnd();
    }
    else
    {
        streamState = StreamFailed;
        updateStreamingUI();
        progressMessages.insertTextAtCaret("ERROR: Failed to start streaming\n");
        progressMessages.moveCaretToEnd();
    }
}

void MainComponent::stopStreaming()
{
    // Set stopping state
    streamState = StreamStopping;
    updateStreamingUI();
    
    if (streamer)
    {
        streamer->stopStreaming();
    }
    
    isStreaming = false;
    streamState = StreamIdle;
    updateStreamingUI();
    
    progressMessages.insertTextAtCaret("Streaming stopped\n");
    progressMessages.moveCaretToEnd();
}

void MainComponent::updateStreamingUI()
{
    switch (streamState)
    {
        case StreamIdle:
            streamButton.setButtonText("Stream Now");
            streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(70, 70, 70)); // Dark gray
            streamButton.setEnabled(true);
            
            // Re-enable editing
            platformSelector.setEnabled(true);
            rtmpKeyEditor.setEnabled(true);
            bitrateEditor.setEnabled(true);
            break;
            
        case StreamPreparing:
            streamButton.setButtonText("Preparing...");
            streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(90, 90, 90)); // Medium gray
            streamButton.setEnabled(false);
            
            // Disable editing while preparing
            platformSelector.setEnabled(false);
            rtmpKeyEditor.setEnabled(false);
            bitrateEditor.setEnabled(false);
            break;
            
        case StreamLive:
            streamButton.setButtonText("LIVE - Stop");
            streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 40)); // Very dark gray for live
            streamButton.setEnabled(true);
            
            // Keep editing disabled while live
            platformSelector.setEnabled(false);
            rtmpKeyEditor.setEnabled(false);
            bitrateEditor.setEnabled(false);
            
            // Force layout update to show health monitoring
            resized();
            break;
            
        case StreamStopping:
            streamButton.setButtonText("Stopping...");
            streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(100, 100, 100)); // Light gray
            streamButton.setEnabled(false);
            
            // Keep editing disabled while stopping
            platformSelector.setEnabled(false);
            rtmpKeyEditor.setEnabled(false);
            bitrateEditor.setEnabled(false);
            break;
            
        case StreamFailed:
            streamButton.setButtonText("Failed - Retry");
            streamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 80, 80)); // Medium gray for failed
            streamButton.setEnabled(true);
            
            // Re-enable editing after failure
            platformSelector.setEnabled(true);
            rtmpKeyEditor.setEnabled(true);
            bitrateEditor.setEnabled(true);
            break;
    }
}

void MainComponent::addProgressMessage(const juce::String& message)
{
    progressMessages.insertTextAtCaret(message + "\n");
    progressMessages.moveCaretToEnd();
}

void MainComponent::updateStreamingAudioSettings()
{
    // Sync streaming audio sources with current UI settings
    if (!streamingBinauralSource || !streamingFilePlayer || !streamingNoiseSource)
        return;
    
    // Safety check: Make sure audio panel components are initialized
    if (!audioPanel.getBinauralTrack() || !audioPanel.getNoiseTrack())
    {
        // Audio panel not ready yet, keep streaming sources muted
        streamingBinauralSource->setGain(0.0f);
        streamingNoiseSource->setMuted(true);
        streamingNoiseSource->setGain(0.0f);
        return;
    }
    
    // Update streaming binaural source to match current UI settings
    if (auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(audioPanel.getBinauralTrack()))
    {
        float gain = binauralTrack->isMuted() ? 0.0f : binauralTrack->getActualGain();
        streamingBinauralSource->setGain(gain);
        // Use the correct frequency methods
        streamingBinauralSource->setLeftFrequency(binauralTrack->getLeftFrequency());
        streamingBinauralSource->setRightFrequency(binauralTrack->getRightFrequency());
    }
    
    // Update streaming noise source to match current UI settings
    if (auto* noiseTrack = audioPanel.getNoiseTrack())
    {
        float gain = noiseTrack->isMuted() ? 0.0f : noiseTrack->getActualGain();
        streamingNoiseSource->setGain(gain);
        streamingNoiseSource->setMuted(noiseTrack->isMuted());
        // Copy noise type if available in your NoiseAudioSource
    }
    
    // Ensure streaming file player gain matches the UI track
    if (auto* fileTrack = dynamic_cast<FileTrackComponent*>(audioPanel.getFileTrack()))
    {
        float gain = fileTrack->isMuted() ? 0.0f : fileTrack->getActualGain();
        if (streamingFilePlayer)
            streamingFilePlayer->setGain(gain);
    }
    
    // Update streaming file player to use same file as local playback
    if (filePlayer && streamingFilePlayer)
    {
        juce::File currentFile = filePlayer->getLoadedFile();
        if (currentFile.existsAsFile())
        {
            streamingFilePlayer->loadFile(currentFile);
            streamingFilePlayer->start();
        }
        // else: no valid file loaded, streaming player unchanged

        streamingFilePlayer->setPlaylist(filePlayer->getPlaylist());
    }
}

void MainComponent::parseFFmpegStats(const juce::String& output)
{
    // Parse FFmpeg output like: frame= 1800 fps= 30 q=23.0 bitrate=6144.0kbits/s speed=1.00x
    if (output.isEmpty() || output.length() > 10000)
        return;

    // Sanitize input - only safe ASCII characters
    juce::String safeOutput;
    for (int i = 0; i < output.length() && i < 2000; ++i)
    {
        juce::juce_wchar ch = output[i];
        if ((ch >= 32 && ch <= 126) || ch == 9 || ch == 10 || ch == 13)
            safeOutput += ch;
    }

    if (safeOutput.isEmpty())
        return;

    // Check for heartbeat messages
    if (safeOutput.contains("FFmpeg monitoring active") || safeOutput.contains("no stats yet"))
    {
        connectionStatusLabel.setText("Status: Monitoring...", juce::dontSendNotification);
        connectionStatusLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
        return;
    }

    // Helper to parse a numeric value after a key
    auto parseValue = [&](const juce::String& key, juce::juce_wchar endChar = ' ') -> juce::String {
        if (!safeOutput.contains(key))
            return {};
        int start = safeOutput.indexOf(key) + key.length();
        while (start < safeOutput.length() && safeOutput[start] == ' ')
            start++;
        if (start >= safeOutput.length())
            return {};
        int end = (endChar == ' ') ? safeOutput.indexOfChar(start, ' ') : safeOutput.indexOfChar(start, endChar);
        if (end == -1) end = safeOutput.length();
        if (end <= start)
            return {};
        juce::String val = safeOutput.substring(start, end).trim();
        return (val.length() < 20) ? val : juce::String();
    };

    // Parse FPS
    juce::String fpsStr = parseValue("fps=");
    if (fpsStr.isNotEmpty())
        streamStats.fps = fpsStr.getFloatValue();

    // Parse bitrate
    juce::String bitrateStr = parseValue("bitrate=", 'k');
    if (bitrateStr.isNotEmpty())
        streamStats.actualBitrate = bitrateStr.getFloatValue();
    else if (streamStats.targetBitrate > 0)
        streamStats.actualBitrate = streamStats.targetBitrate;

    // Parse speed
    juce::String speedStr = parseValue("speed=", 'x');
    if (speedStr.isNotEmpty())
        streamStats.speed = speedStr.getFloatValue();
    else
        streamStats.speed = 1.0f;

    // Parse frame count
    juce::String frameStr = parseValue("frame=");
    if (frameStr.isNotEmpty())
        streamStats.totalFrames = frameStr.getLargeIntValue();

    // Parse dropped frames
    juce::String dropStr = parseValue("drop=");
    if (dropStr.isNotEmpty())
        streamStats.droppedFrames = dropStr.getIntValue();
    
    // Extract duplicate frames using safe string operations
    if (safeOutput.contains("dup="))
    {
        try 
        {
            int dupStart = safeOutput.indexOf("dup=") + 4;
            if (dupStart >= 4 && dupStart < safeOutput.length())
            {
                int dupEnd = safeOutput.indexOfChar(dupStart, ' ');
                if (dupEnd == -1) dupEnd = safeOutput.length();
                
                if (dupEnd > dupStart && dupEnd <= safeOutput.length())
                {
                    juce::String dupStr = safeOutput.substring(dupStart, dupEnd).trim();
                    if (dupStr.isNotEmpty() && dupStr.length() < 20)
                    {
                        streamStats.duplicateFrames = dupStr.getIntValue();
                    }
                }
            }
        }
        catch (...) { }
    }
    
    // Update target bitrate from UI
    streamStats.targetBitrate = bitrateEditor.getText().getFloatValue();
    
    // If no data was parsed, show test data for UI debugging
    static int parseCallCount = 0;
    parseCallCount++;
    
    // Count successful parses
    static int successfulParses = 0;
    bool parsedSomething = false;
    
    if (streamStats.actualBitrate > 0 || streamStats.fps > 0 || streamStats.speed > 0)
    {
        successfulParses++;
        parsedSomething = true;
    }

    // Use test data only if we haven't had any successful parses after many attempts
    if (!parsedSomething && successfulParses == 0 && parseCallCount > 10)
    {
        
        // Generate test data to verify UI is working
        streamStats.actualBitrate = streamStats.targetBitrate * 0.85f; // 85% of target
        streamStats.fps = 29.5f;
        streamStats.speed = 0.98f;
        streamStats.droppedFrames = 2;
        streamStats.duplicateFrames = 1;
        streamStats.totalFrames = parseCallCount * 30; // Simulate frame count
        
        addProgressMessage("Using test data - check FFmpeg stats output in logs");
    }
    
    // Calculate health status
    float bitrateRatio = streamStats.targetBitrate > 0 ? streamStats.actualBitrate / streamStats.targetBitrate : 0;
    streamStats.isHealthy = (streamStats.speed >= 0.95f && 
                            streamStats.fps >= 28.0f && 
                            bitrateRatio >= 0.85f &&
                            streamStats.droppedFrames < 100);
    
    // Update UI on message thread
    updateStreamHealthDisplay();
}

void MainComponent::updateStreamHealthDisplay()
{
    // Safe UI updates with validated float-to-string conversions
    try 
    {
        // Validate float values and provide defaults for invalid ones
        float safeBitrateActual = (std::isfinite(streamStats.actualBitrate) && streamStats.actualBitrate >= 0) ? streamStats.actualBitrate : 0.0f;
        float safeBitrateTarget = (std::isfinite(streamStats.targetBitrate) && streamStats.targetBitrate >= 0) ? streamStats.targetBitrate : 0.0f;
        float safeBitrateRatio = safeBitrateTarget > 0 ? (safeBitrateActual / safeBitrateTarget) : 0.0f;
        
        // Ensure ratio is finite and reasonable
        if (!std::isfinite(safeBitrateRatio) || safeBitrateRatio < 0) safeBitrateRatio = 0.0f;
        if (safeBitrateRatio > 10.0f) safeBitrateRatio = 10.0f; // Cap at 1000%
        
        juce::Colour bitrateColor;
        if (safeBitrateRatio < 0.8f) bitrateColor = juce::Colours::red;         // Bad
        else if (safeBitrateRatio < 0.95f) bitrateColor = juce::Colours::orange; // Warning
        else bitrateColor = juce::Colours::green;                               // Good
        
        // Safe string construction with bounds checking
        juce::String bitrateText = "Bitrate: ";
        bitrateText += juce::String((int)safeBitrateActual); // Use int conversion to avoid float precision issues
        bitrateText += "/";
        bitrateText += juce::String((int)safeBitrateTarget);
        bitrateText += " kbps (";
        bitrateText += juce::String((int)(safeBitrateRatio * 100));
        bitrateText += "%)";
        
        bitrateActualLabel.setText(bitrateText, juce::dontSendNotification);
        bitrateActualLabel.setColour(juce::Label::textColourId, bitrateColor);
        
        // FPS display with safe float handling
        float safeFps = (std::isfinite(streamStats.fps) && streamStats.fps >= 0) ? streamStats.fps : 0.0f;
        if (safeFps > 999.0f) safeFps = 999.0f; // Cap at reasonable value
        
        juce::Colour fpsColor;
        if (safeFps < 25) fpsColor = juce::Colours::red;
        else if (safeFps < 29) fpsColor = juce::Colours::orange;
        else fpsColor = juce::Colours::green;
        
        juce::String fpsText = "FPS: ";
        fpsText += juce::String((int)safeFps) + "."; // Integer part
        fpsText += juce::String((int)(safeFps * 10) % 10);   // First decimal digit
        
        fpsLabel.setText(fpsText, juce::dontSendNotification);
        fpsLabel.setColour(juce::Label::textColourId, fpsColor);
        
        // Dropped frames with safe integer handling
        int safeDropped = (streamStats.droppedFrames >= 0 && streamStats.droppedFrames < 999999) ? streamStats.droppedFrames : 0;
        int safeDup = (streamStats.duplicateFrames >= 0 && streamStats.duplicateFrames < 999999) ? streamStats.duplicateFrames : 0;
        
        juce::Colour dropColor;
        if (safeDropped > 100) dropColor = juce::Colours::red;
        else if (safeDropped > 10) dropColor = juce::Colours::orange;
        else dropColor = juce::Colours::green;
        
        juce::String dropText = "Dropped: ";
        dropText += juce::String(safeDropped);
        if (safeDup > 0) 
        {
            dropText += " (Dup: ";
            dropText += juce::String(safeDup);
            dropText += ")";
        }
        
        droppedFramesLabel.setText(dropText, juce::dontSendNotification);
        droppedFramesLabel.setColour(juce::Label::textColourId, dropColor);
        
        // Connection status with safe speed handling
        float safeSpeed = (std::isfinite(streamStats.speed) && streamStats.speed >= 0) ? streamStats.speed : 0.0f;
        if (safeSpeed > 99.0f) safeSpeed = 99.0f; // Cap at reasonable value
        
        juce::Colour connColor;
        juce::String connText;
        if (!streamStats.isHealthy) {
            connColor = juce::Colours::red;
            connText = "Status: Poor (";
        } else if (safeSpeed < 0.98f) {
            connColor = juce::Colours::orange;
            connText = "Status: Fair (";
        } else {
            connColor = juce::Colours::green;
            connText = "Status: Good (";
        }
        
        // Manual speed formatting (e.g., "1.00x")
        connText += juce::String((int)safeSpeed) + ".";  // Integer part
        connText += juce::String((int)(safeSpeed * 10) % 10);  // First decimal
        connText += juce::String((int)(safeSpeed * 100) % 10); // Second decimal
        connText += "x)";
        
        connectionStatusLabel.setText(connText, juce::dontSendNotification);
        connectionStatusLabel.setColour(juce::Label::textColourId, connColor);
    }
    catch (...)
    {
        bitrateActualLabel.setText("Bitrate: -- kbps", juce::dontSendNotification);
        fpsLabel.setText("FPS: --", juce::dontSendNotification);
        droppedFramesLabel.setText("Dropped: --", juce::dontSendNotification);
        connectionStatusLabel.setText("Status: Error", juce::dontSendNotification);
    }
}
