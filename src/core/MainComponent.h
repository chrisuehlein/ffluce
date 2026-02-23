/*
  ==============================================================================
    MainComponent.h - Main UI Component
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "../ui/resources/ImageResources.h"
#include "../ui/panels/AudioPanel.h"
#include "../ui/panels/VideoPanel.h"

/** Floating overlay showing elapsed render time */
class RenderTimerOverlay : public juce::Component,
                           private juce::Timer
{
public:
    RenderTimerOverlay()
    {
        startTime = juce::Time::getCurrentTime();

        juce::Font labelFont(24.0f);
        labelFont = labelFont.boldened();
        elapsedTimeLabel.setFont(labelFont);
        elapsedTimeLabel.setJustificationType(juce::Justification::centred);
        elapsedTimeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        elapsedTimeLabel.setText("00:00:00", juce::dontSendNotification);
        addAndMakeVisible(elapsedTimeLabel);

        startTimer(100);
    }
    
    ~RenderTimerOverlay() override
    {
        stopTimer();
    }
    
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 180));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);

        g.setColour(juce::Colours::orange);
        g.drawRoundedRectangle(getLocalBounds().toFloat(), 10.0f, 2.0f);

        juce::Font titleFont(18.0f);
        titleFont = titleFont.boldened();
        g.setFont(titleFont);
        g.drawText("RENDER TIME", getLocalBounds().removeFromTop(40),
                   juce::Justification::centred, true);
    }

    void resized() override
    {
        elapsedTimeLabel.setBounds(0, 40, getWidth(), 40);
    }
    
    double getElapsedSeconds() const
    {
        return (juce::Time::getCurrentTime() - startTime).inSeconds();
    }
    
    juce::String getElapsedTimeString() const
    {
        return formatTimeString(getElapsedSeconds());
    }
    
    void reset()
    {
        startTime = juce::Time::getCurrentTime();
        elapsedTimeLabel.setText("00:00:00", juce::dontSendNotification);
        repaint();
    }
    
private:
    void timerCallback() override
    {
        double elapsedSeconds = getElapsedSeconds();
        elapsedTimeLabel.setText(formatTimeString(elapsedSeconds), juce::dontSendNotification);
        repaint();
    }
    
    juce::String formatTimeString(double seconds) const
    {
        const int hours = static_cast<int>(seconds) / 3600;
        const int minutes = (static_cast<int>(seconds) % 3600) / 60;
        const int secs = static_cast<int>(seconds) % 60;
        
        return juce::String::formatted("%02d:%02d:%02d", hours, minutes, secs);
    }
    
    juce::Label elapsedTimeLabel;
    juce::Time startTime;
};

/** Master audio meter for streaming output */
class MasterMeterComponent : public juce::Component,
                             private juce::Timer
{
public:
    MasterMeterComponent()
    {
        leftLevel = 0.0f;
        rightLevel = 0.0f;
        leftPeak = 0.0f;
        rightPeak = 0.0f;
        startTimerHz(30);
    }

    void setLevels(float left, float right)
    {
        leftLevel = left;
        rightLevel = right;

        if (left > leftPeak) leftPeak = left;
        if (right > rightPeak) rightPeak = right;

        leftPeak *= 0.95f;
        rightPeak *= 0.95f;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced(2);

        auto titleArea = bounds.removeFromTop(16);
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText("MASTER", titleArea, juce::Justification::centred);

        bounds.removeFromTop(2);

        auto scaleArea = bounds.removeFromRight(25);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(9.0f);

        int meterHeight = bounds.getHeight() - 15;

        int zeroDB_Y = bounds.getY() + (int)(meterHeight * 0.25f);
        g.drawText("0", scaleArea.getX(), zeroDB_Y - 5, 20, 10, juce::Justification::centredLeft);

        int minus12dB_Y = bounds.getY() + (int)(meterHeight * 0.5f);
        g.drawText("-12", scaleArea.getX(), minus12dB_Y - 5, 20, 10, juce::Justification::centredLeft);

        int minus24dB_Y = bounds.getY() + (int)(meterHeight * 0.75f);
        g.drawText("-24", scaleArea.getX(), minus24dB_Y - 5, 20, 10, juce::Justification::centredLeft);

        auto meterBounds = bounds.withTrimmedRight(25);

        g.setColour(juce::Colours::black);
        g.fillRect(meterBounds);

        int halfWidth = meterBounds.getWidth() / 2;
        auto leftBounds = meterBounds.removeFromLeft(halfWidth - 1);
        drawMeter(g, leftBounds, leftLevel, leftPeak, true);

        meterBounds.removeFromLeft(2);

        auto rightBounds = meterBounds;
        drawMeter(g, rightBounds, rightLevel, rightPeak, false);

        g.setColour(juce::Colours::white);
        g.setFont(10.0f);
        g.drawText("L", leftBounds.removeFromBottom(12), juce::Justification::centred);
        g.drawText("R", rightBounds.removeFromBottom(12), juce::Justification::centred);
    }
    
private:
    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float level, float peak, bool /*isLeft*/)
    {
        auto meterBounds = bounds.reduced(2).removeFromTop(bounds.getHeight() - 15);

        float levelDb = juce::Decibels::gainToDecibels(level, -60.0f);
        float peakDb = juce::Decibels::gainToDecibels(peak, -60.0f);

        float levelNorm = (levelDb + 60.0f) / 60.0f;
        float peakNorm = (peakDb + 60.0f) / 60.0f;

        int levelHeight = (int)(meterBounds.getHeight() * levelNorm);
        int peakHeight = (int)(meterBounds.getHeight() * peakNorm);

        auto levelRect = meterBounds.removeFromBottom(levelHeight);

        if (levelNorm < 0.75f)
            g.setColour(juce::Colours::green.interpolatedWith(juce::Colours::yellow, levelNorm / 0.75f));
        else
            g.setColour(juce::Colours::yellow.interpolatedWith(juce::Colours::red, (levelNorm - 0.75f) / 0.25f));

        g.fillRect(levelRect);

        if (peak > 0.001f)
        {
            g.setColour(juce::Colours::white);
            int peakY = meterBounds.getBottom() - peakHeight;
            g.drawLine(meterBounds.getX(), peakY, meterBounds.getRight(), peakY, 1.0f);
        }
    }
    
    void timerCallback() override
    {
        repaint();
    }
    
    float leftLevel, rightLevel;
    float leftPeak, rightPeak;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterMeterComponent)
};

/** Button with an image overlay */
class ImageTextButton : public juce::TextButton
{
public:
    ImageTextButton() = default;

    void setImage(const juce::Image& img)
    {
        buttonImage = img;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        if (buttonImage.isValid())
        {
            auto bounds = getLocalBounds().reduced(4);
            g.drawImage(buttonImage, bounds.toFloat(), juce::RectanglePlacement::centred);
        }
    }
    
private:
    juce::Image buttonImage;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageTextButton)
};

class MainComponent : public juce::AudioAppComponent,
    private juce::Button::Listener,
    private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    
    // Access to audio panel for rendering
    AudioPanel* getAudioPanel() { return &audioPanel; }

private:
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    
    void saveProject();
    void loadProject();
    void clearProject();
    void startRendering();
    void updateTransportState();
    void rebuildAudioMixers();
    void rebuildLocalMixerInputs();
    void rebuildStreamingMixerInputs();
    void startLocalFileTracks();
    void stopLocalFileTracks();
    void resetLocalFileTrackPositions(double positionSeconds);
    bool isPlaying() const { return transportState == Playing; }
    
    juce::MixerAudioSource mixer;                    // Local playback mixer (controlled by play/stop)
    juce::MixerAudioSource streamingMixer;          // Always-on mixer for streaming audio
    juce::CriticalSection audioGraphLock;
    
    // Transport state
    enum TransportState
    {
        Stopped,
        Starting,
        Playing,
        Pausing,
        Paused,
        Stopping
    };
    
    TransportState transportState = Stopped;
    
    // UI Sections
    juce::Label videoSectionLabel{ {}, "VIDEO" };
    juce::Label audioSectionLabel{ {}, "AUDIO" };
    juce::TextButton addTrackButton{ "Add Track" };
    
    // Main panels
    AudioPanel audioPanel;
    VideoPanel videoPanel;
    
    // Top toolbar - app logo and presets
    juce::ImageComponent appLogoComponent;
    juce::Label dateLabel{ {}, "" };  // Shows current date
    juce::Label presetsLabel{ {}, "PRESETS" };
    juce::TextButton saveButton{ "Save" };
    juce::TextButton loadButton{ "Load" };
    juce::TextButton clearButton{ "Clear" };
    
    // Transport controls
    ImageTextButton playPauseButton;
    ImageTextButton stopButton;
    juce::Image playImage, stopImage;
    
    // Bottom bar with properties
    juce::Label outputLabel{ {}, "OUTPUT" };
    
    juce::Label durationLabel{ {}, "DURATION" };
    juce::TextEditor hoursEditor;    // For hours
    juce::TextEditor minutesEditor;  // For minutes
    juce::TextEditor secondsEditor;  // For seconds
    juce::Label hoursLabel{ {}, "h" };
    juce::Label minutesLabel{ {}, "m" };
    juce::Label secondsLabel{ {}, "s" };
    
    juce::Label fadeInLabel{ {}, "FADE IN (SEC/MM:SS)" };
    juce::TextEditor fadeInEditor;
    
    juce::Label fadeOutLabel{ {}, "FADE OUT (SEC/MM:SS)" };
    juce::TextEditor fadeOutEditor;
    
    juce::TextButton renderButton{ "Render" };
    
    // Streaming controls
    juce::Label streamingLabel{ {}, "STREAMING" };
    juce::Label platformLabel{ {}, "PLATFORM" };
    juce::ComboBox platformSelector;
    juce::Label rtmpKeyLabel{ {}, "RTMP KEY" };
    juce::TextEditor rtmpKeyEditor;
    juce::Label bitrateLabel{ {}, "BITRATE (KBPS)" };
    juce::TextEditor bitrateEditor;
    juce::TextButton streamButton{ "Stream Now" };
    
    // Progress area
    juce::TextEditor progressMessages;
    
    // Master meter
    MasterMeterComponent masterMeter;
    
    // Stream health monitoring
    juce::Label streamHealthLabel{ {}, "Stream Health:" };
    juce::Label bitrateActualLabel{ {}, "Bitrate: --" };
    juce::Label fpsLabel{ {}, "FPS: --" };
    juce::Label droppedFramesLabel{ {}, "Dropped: 0" };
    juce::Label connectionStatusLabel{ {}, "Status: --" };
    
    // Health monitoring background panel
    class HealthBackgroundPanel : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override
        {
            // Draw semi-transparent dark background
            g.setColour(juce::Colour::fromRGBA(0, 0, 0, 200));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
            
            // Draw subtle border
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 50));
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 4.0f, 1.0f);
        }
    };
    
    HealthBackgroundPanel healthBackgroundPanel;
    
    struct StreamStats {
        float actualBitrate{0};
        float targetBitrate{0};
        float fps{0};
        float speed{0};
        int droppedFrames{0};
        int duplicateFrames{0};
        juce::int64 totalFrames{0};
        bool isHealthy{false};
    } streamStats;
    
    // Background colors - more visually distinctive
    juce::Colour videoSectionColor = juce::Colour(60, 60, 80);   // Blue-ish
    juce::Colour audioSectionColor = juce::Colour(60, 80, 60);   // Green-ish
    
    // Rendering thread
    std::unique_ptr<juce::Thread> renderingThread;
    bool isRendering = false;
    
    // Keep file choosers alive between function calls
    std::unique_ptr<juce::FileChooser> saveProjectChooser;
    std::unique_ptr<juce::FileChooser> loadProjectChooser;
    
    // Streaming - FFLUCE uses YoutubeStreamer for live streaming
    // This component handles infinite looping of video clips with crossfades
    // and real-time audio mixing for YouTube/Twitch/Custom RTMP streaming
    std::unique_ptr<class YoutubeStreamer> streamer;
    bool isStreaming = false;
    
    // Stream button states
    enum StreamState
    {
        StreamIdle,
        StreamPreparing,
        StreamLive,
        StreamStopping,
        StreamFailed
    };
    
    StreamState streamState = StreamIdle;
    
    // Helper functions for streaming
    void startStreaming();
    void stopStreaming();
    void updateStreamingUI();
    
    // Helper function for progress messages
    void addProgressMessage(const juce::String& message);
    
    // Helper function to sync streaming audio settings with UI
    void updateStreamingAudioSettings();
    
    // Stream health monitoring
    void parseFFmpegStats(const juce::String& output);
    void updateStreamHealthDisplay();

    // Demo assets
    void loadDemoClipsIfAvailable();
    juce::File findDemoAssetsVideoDir() const;
    
    // Render timer overlay - will be a floating window on top of the UI
    std::unique_ptr<RenderTimerOverlay> renderTimerOverlay;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
