#pragma once
#include <JuceHeader.h>
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "RenderTypes.h"
#include "RenderManagerCore.h"
#include <vector>

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
     * @param binauralSources Binaural audio sources to render from
     * @param filePlayers File audio sources to render from
     * @param noiseSources Noise audio sources to render from
     */
    RenderManager(const std::vector<BinauralAudioSource*>& binauralSources,
                  const std::vector<FilePlayerAudioSource*>& filePlayers,
                  const std::vector<NoiseAudioSource*>& noiseSources);
    
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
