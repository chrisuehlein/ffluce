#pragma once
#include <JuceHeader.h>
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "RenderTypes.h"
#include "RenderManagerCore.h"

/**
 * Compatibility class that forwards calls to the new modular system.
 * This ensures existing code can continue to work without changes.
 */
class RenderManager 
{
public:
    // Forward typedef for compatibility
    using RenderState = RenderTypes::RenderState;
    using VideoClipInfo = RenderTypes::VideoClipInfo;
    using OverlayClipInfo = RenderTypes::OverlayClipInfo;
    
    /**
     * Creates a new RenderManager.
     * @param audioSource The binaural audio source to render from
     * @param filePlayer The file audio source to render from (optional)
     * @param noiseSource The noise audio source to render from (optional)
     */
    RenderManager(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer = nullptr, NoiseAudioSource* noiseSource = nullptr);
    
    ~RenderManager();
    
    /**
     * Starts the rendering process with the specified parameters.
     * Forwards to the core implementation.
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
    RenderState getState() const;
    
    /** Returns true if rendering is in progress. */
    bool isRendering() const;
    
    /** Returns the current status message. */
    juce::String getStatusMessage() const;

private:
    // The actual implementation
    std::unique_ptr<RenderManagerCore> core;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RenderManager)
};