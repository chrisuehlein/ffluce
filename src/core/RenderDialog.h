#pragma once
#include <JuceHeader.h>
#include "../rendering/RenderManager.h"
#include "../rendering/RenderTypes.h"
#include "../audio/NoiseAudioSource.h"

/**
 * Dialog for initiating and monitoring the rendering process
 */
// Custom progress bar that uses an external progress value
class CustomProgressBar : public juce::Component
{
public:
    CustomProgressBar(double& progressRef) : progress(progressRef) {}
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(bounds);
        
        bounds.reduce(1, 1);
        g.setColour(juce::Colours::green);
        g.fillRect(bounds.removeFromLeft(static_cast<int>(bounds.getWidth() * progress)));
        
        // Draw percentage text
        if (showPercentage)
        {
            g.setColour(juce::Colours::white);
            g.setFont(14.0f);
            const juce::String text = juce::String(static_cast<int>(progress * 100)) + "%";
            g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
        }
    }
    
    // Compatibility method to match JUCE's ProgressBar API
    void setPercentageDisplay(bool shouldDisplayPercentage)
    {
        showPercentage = shouldDisplayPercentage;
        repaint();
    }
    
private:
    double& progress;
    bool showPercentage = false;
};

class RenderDialog : public juce::Component,
                     public juce::Button::Listener,
                     private juce::Timer
{
public:
    // Define quality levels as public enum
    enum class QualityPreset {
        Highest,   // Slow/veryslow preset, best quality (CRF 18)
        Balanced,  // Medium preset, good quality (CRF 20)
        Fast,      // Veryfast preset, decent quality (CRF 22)
        Draft      // Ultrafast preset, lower quality (CRF 28)
    };
    
    /**
     * Creates a new render dialog
     * @param binauralSource The binaural audio source
     * @param filePlayer The file audio player source
     * @param noiseSource The noise audio source
     * @param introClips Vector of intro video clips
     * @param loopClips Vector of loop video clips
     * @param duration Total duration in seconds
     * @param fadeIn Fade-in duration in seconds
     * @param fadeOut Fade-out duration in seconds
     */
    RenderDialog(
        BinauralAudioSource* binauralSource,
        FilePlayerAudioSource* filePlayer,
        NoiseAudioSource* noiseSource,
        const std::vector<RenderManager::VideoClipInfo>& introClips,
        const std::vector<RenderManager::VideoClipInfo>& loopClips,
        const std::vector<RenderManager::OverlayClipInfo>& overlayClips,
        double duration,
        double fadeIn,
        double fadeOut);
        
    ~RenderDialog() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void buttonClicked(juce::Button* button) override;
    
    // Show the dialog as a modal window
    void showDialog(juce::Component* parent);
    
private:
    void timerCallback() override;
    
    // Setup UI elements
    void setupUI();
    
    // Start the rendering process
    void startRender();
    
    // Called when the render process finishes
    void renderFinished(bool success);
    
    // Update file extension based on audio-only toggle
    void updateFileExtension();
    
    // Update quality description based on selected preset
    void updateQualityDescription();
    
    // Get encoding parameters based on quality preset
    juce::String getEncodingParams(bool useNvidia, bool isFinalEncode) const;

    juce::File generateDefaultOutputFile(bool audioOnly) const;
    juce::String buildRenderDescriptor() const;
    static juce::String sanitizeToken(const juce::String& token);
    static juce::String formatValueToken(double value, int decimals = 2);
    
    // UI Components
    juce::Label titleLabel{"", "Export Project"};
    juce::Label infoLabel{"", "Create a video file with all clips and audio"};
    juce::Label statusLabel{"", "Ready to render"};
    CustomProgressBar progressBar;
    juce::TextButton renderButton{"Render"};
    juce::TextButton cancelButton{"Cancel"};
    juce::TextButton closeButton{"Close"};
    
    // Elapsed time display
    juce::Label elapsedTimeLabel{"", "Elapsed time:"};
    juce::Label elapsedTimeValue{"", "00:00:00"};
    
    // File path selection
    juce::Label fileLabel{"", "Output file:"};
    juce::TextEditor filePathEditor;
    juce::TextButton browseButton{"..."};
    juce::File outputFile;
    
    // Rendering options
    juce::ToggleButton useNvidiaAcceleration{"Use NVIDIA GPU acceleration (if available)"};
    juce::Label nvidiaInfoLabel{"", "Uses NVENC for faster encoding on NVIDIA GPUs"};
    juce::ToggleButton audioOnlyToggle{"RENDER AUDIO ONLY (NO VIDEO)"};
    juce::Label audioOnlyInfoLabel{"", "Outputs only the audio track as a WAV file"};
    
    // Quality settings
    juce::Label qualityLabel{"", "Quality:"};
    juce::ComboBox qualityCombo;
    juce::Label qualityDescription{"", ""};
    
    // Member variables for rendering
    BinauralAudioSource* binauralSource;
    FilePlayerAudioSource* filePlayer;
    NoiseAudioSource* noiseSource;
    std::vector<RenderManager::VideoClipInfo> introClips;
    std::vector<RenderManager::VideoClipInfo> loopClips;
    std::vector<RenderManager::OverlayClipInfo> overlayClips;
    double duration;
    double fadeIn;
    double fadeOut;
    juce::Time renderStartTime;
    double progress = 0.0;
    
    // Render manager
    std::unique_ptr<RenderManager> renderManager;
    
    // Keep file chooser alive during async operations
    std::unique_ptr<juce::FileChooser> fileChooser;
};
