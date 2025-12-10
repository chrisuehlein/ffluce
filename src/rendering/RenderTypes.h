#pragma once
#include <JuceHeader.h>

// Forward declarations
class RenderManagerCore;

/**
 * Common types used across the rendering system.
 * These types are shared by multiple components to ensure consistency.
 */
namespace RenderTypes 
{
    /** Information about a video clip to be rendered */
    struct VideoClipInfo
    {
        juce::File file;
        double startTime;   // Start time offset within the clip
        double duration;    // Duration to use from the clip
        double crossfade;   // Duration of crossfade with next clip
        bool isIntroClip;   // Whether this is an intro clip (vs loop clip)
    };
    
    /** Information about an overlay clip to be rendered */
    struct OverlayClipInfo
    {
        juce::File file;        // The overlay video file (with alpha transparency)
        double duration;        // Duration of the overlay clip
        double frequencySecs;   // How often the overlay appears (in seconds)
        double startTimeSecs;   // When the first overlay should appear (in seconds from start)
    };
    
    /** Represents the current status of the render process */
    enum class RenderState
    {
        Idle,
        Starting,
        Preparing,
        RenderingAudio,
        ProcessingVideoClips,
        ProcessingClips,      // Alternative name used in some places
        RenderingCrossfades,
        AssemblingVideo,
        AssemblingTimeline,
        ApplyingFadeIn,
        ApplyingFadeOut,
        Finalizing,
        Completed,
        Failed,
        Cancelled
    };
}