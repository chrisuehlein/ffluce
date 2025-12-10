/*
  ==============================================================================

    VideoPanel.h
    Created: 18 Feb 2025 7:24:30pm
    Author:  chris

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../../rendering/RenderTypes.h"

/**
    VideoPanel with two sections:
      - Intro videos + "Add Intro"
      - Loop videos + "Add Loop"
    Each is a table with columns for clip name, duration, and crossfade.
    A preview box shows a thumbnail when a video is selected.
*/
// Forward declarations of our table models
class IntroTableModel;
class LoopTableModel;
class OverlayTableModel;


// Forward declare our VideoPreviewComponent class
class VideoPreviewComponent;

class VideoPanel : public juce::Component,
                   private juce::Button::Listener,
                   private juce::Timer
{
public:
    VideoPanel();
    ~VideoPanel() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Timer function to handle UI updates
    void timerCallback();
    
    // Called when selection changes in any table
    void introSelectionChanged(int lastRowSelected);
    void loopSelectionChanged(int lastRowSelected);
    void overlaySelectionChanged(int lastRowSelected);
    
    
    // Move clips up/down
    void moveClipUp(int index, bool isIntroClip);
    void moveClipDown(int index, bool isIntroClip);
    void updateClipList() { introTable.updateContent(); loopTable.updateContent(); }
    
    // Project save/load methods
    void clearAllClips() { 
        introClips.clear(); 
        loopClips.clear();
        overlayClips.clear();
        introTable.updateContent(); 
        loopTable.updateContent(); 
        overlayTable.updateContent();
    }
    int getNumIntroClips() const { return introClips.size(); }
    int getNumLoopClips() const { return loopClips.size(); }
    int getNumOverlayClips() const { return overlayClips.size(); }
    
    // Add clip with specified duration and crossfade
    // If duration <= 0, use the video's native duration
    void addIntroClip(const juce::File& videoFile, double duration, double crossfade);
    void addLoopClip(const juce::File& videoFile, double duration, double crossfade);
    
    // Add overlay clip with specified duration, frequency, and start time
    // If duration <= 0, use the video's native duration
    void addOverlayClip(const juce::File& videoFile, double duration, double frequencySecs, double startTimeSecs);
    
    // Helper method to get video duration using FFmpeg
    double getVideoDuration(const juce::File& videoFile);
    
    // Public accessors for clip data (for streaming) - using simple data conversion
    std::vector<RenderTypes::VideoClipInfo> getIntroClipsForStreaming() const;
    std::vector<RenderTypes::VideoClipInfo> getLoopClipsForStreaming() const;  
    std::vector<RenderTypes::OverlayClipInfo> getOverlayClipsForStreaming() const;
    
    // Get clip data for saving
    struct ClipData
    {
        juce::String filePath;
        double duration;
        double crossfade;
    };
    
    struct OverlayClipData
    {
        juce::String filePath;
        double duration;         // Duration in seconds
        double frequencySecs;    // Frequency in seconds (how often the overlay appears)
        double startTimeSecs;    // When first overlay appears (in seconds from start)
    };
    
    ClipData getIntroClipData(int index) const;
    ClipData getLoopClipData(int index) const;
    OverlayClipData getOverlayClipData(int index) const;

private:
    void buttonClicked(juce::Button* b) override;
    std::unique_ptr<juce::Image> createThumbnail(const juce::File& videoFile);
    
    // Background thumbnail loader
    class ThumbnailLoader : public juce::Thread
    {
    public:
        ThumbnailLoader(VideoPanel& owner) 
            : juce::Thread("ThumbnailLoader"), panel(owner) {}
            
        void loadThumbnail(int clipIndex, bool isIntro, const juce::File& videoFile)
        {
            // Add request to queue
            {
                const juce::ScopedLock lock(queueMutex);
                ThumbnailRequest request{clipIndex, isIntro, videoFile};
                requestQueue.add(request);
            }
            
            // Start thread if not running
            if (!isThreadRunning())
                startThread();
        }
        
        void run() override
        {
            while (!threadShouldExit())
            {
                // Get next request
                ThumbnailRequest request;
                bool hasRequest = false;
                
                {
                    const juce::ScopedLock lock(queueMutex);
                    if (requestQueue.size() > 0)
                    {
                        request = requestQueue[0];
                        requestQueue.remove(0);
                        hasRequest = true;
                    }
                }
                
                if (hasRequest)
                {
                    // Generate thumbnail
                    auto thumbnailImage = panel.createThumbnail(request.file);
                    
                    // Update on message thread
                    juce::MessageManager::callAsync([this, request, thumbnailPtr = thumbnailImage.release()]() {
                        panel.setThumbnailForClip(request.clipIndex, request.isIntro, std::unique_ptr<juce::Image>(thumbnailPtr));
                    });
                }
                else
                {
                    // No requests, sleep before checking again
                    wait(500);
                }
                
                // Exit if needed
                if (threadShouldExit())
                    break;
            }
        }
        
    private:
        struct ThumbnailRequest
        {
            int clipIndex;
            bool isIntro;
            juce::File file;
        };
        
        VideoPanel& panel;
        juce::Array<ThumbnailRequest> requestQueue;
        juce::CriticalSection queueMutex;
    };
    
    void setThumbnailForClip(int clipIndex, bool isIntro, std::unique_ptr<juce::Image> thumbnail);
    void setThumbnailForOverlay(int clipIndex, std::unique_ptr<juce::Image> thumbnail);
    void queueThumbnailGeneration(int clipIndex, bool isIntro, const juce::File& videoFile);
    
    // Video clip structure
    class VideoClip
    {
    public:
        VideoClip() = default;
        
        // Custom copy constructor and assignment operator
        VideoClip(const VideoClip& other)
            : filePath(other.filePath), duration(other.duration), crossfade(other.crossfade)
        {
            if (other.thumbnail)
                thumbnail.reset(new juce::Image(*other.thumbnail));
            thumbnailLoaded = other.thumbnailLoaded;
            isThumbnailLoading = other.isThumbnailLoading;
        }
        
        VideoClip& operator=(const VideoClip& other)
        {
            filePath = other.filePath;
            duration = other.duration;
            crossfade = other.crossfade;
            
            if (other.thumbnail)
                thumbnail.reset(new juce::Image(*other.thumbnail));
            else
                thumbnail.reset();
                
            thumbnailLoaded = other.thumbnailLoaded;
            isThumbnailLoading = other.isThumbnailLoading;
                
            return *this;
        }
        
        juce::String filePath;
        double duration = 0.0;          // Will be set to native duration when added
        double crossfade = 1.0;         // In seconds
        std::unique_ptr<juce::Image> thumbnail;
        bool thumbnailLoaded = false;   // Flag to track if thumbnail was loaded
        bool isThumbnailLoading = false; // Flag to prevent multiple load attempts
    };
    
    // Overlay clip structure
    class OverlayClip
    {
    public:
        OverlayClip() = default;
        
        // Custom copy constructor and assignment operator
        OverlayClip(const OverlayClip& other)
            : filePath(other.filePath), duration(other.duration), frequencySecs(other.frequencySecs),
              startTimeSecs(other.startTimeSecs)
        {
            if (other.thumbnail)
                thumbnail.reset(new juce::Image(*other.thumbnail));
            thumbnailLoaded = other.thumbnailLoaded;
            isThumbnailLoading = other.isThumbnailLoading;
        }
        
        OverlayClip& operator=(const OverlayClip& other)
        {
            filePath = other.filePath;
            duration = other.duration;
            frequencySecs = other.frequencySecs;
            startTimeSecs = other.startTimeSecs;
            
            if (other.thumbnail)
                thumbnail.reset(new juce::Image(*other.thumbnail));
            else
                thumbnail.reset();
                
            thumbnailLoaded = other.thumbnailLoaded;
            isThumbnailLoading = other.isThumbnailLoading;
                
            return *this;
        }
        
        juce::String filePath;
        double duration = 0.0;          // Will be set to native duration when added
        double frequencySecs = 5.0;     // How often the overlay appears (in seconds)
        double startTimeSecs = 10.0;    // Time in seconds when overlay first appears
        std::unique_ptr<juce::Image> thumbnail;
        bool thumbnailLoaded = false;   // Flag to track if thumbnail was loaded
        bool isThumbnailLoading = false; // Flag to prevent multiple load attempts
    };
    
    // Store clips for each section
    juce::Array<VideoClip> introClips;
    juce::Array<VideoClip> loopClips;
    juce::Array<OverlayClip> overlayClips;
    
    // Track active list for preview display only
    enum ListType { IntroList, LoopList, OverlayList };
    ListType activeList = IntroList;
    
    // UI Components - using separate table models
    juce::TableListBox introTable;
    juce::TableListBox loopTable;
    juce::TableListBox overlayTable;
    
    // Each table has its own model
    std::unique_ptr<IntroTableModel> introModel;
    std::unique_ptr<LoopTableModel> loopModel;
    std::unique_ptr<OverlayTableModel> overlayModel;
    
    // Access methods for table models
    juce::Array<VideoClip>& getIntroClips() { return introClips; }
    juce::Array<VideoClip>& getLoopClips() { return loopClips; }
    juce::Array<OverlayClip>& getOverlayClips() { return overlayClips; }
    
    // Friend classes to allow the table models to access this class
    friend class IntroTableModel;
    friend class LoopTableModel;
    friend class OverlayTableModel;
    
    juce::TextButton addIntroButton { "+" };
    juce::TextButton addLoopButton  { "+" };
    juce::TextButton addOverlayButton { "+" };
    juce::TextButton deleteIntroButton { "X" };
    juce::TextButton deleteLoopButton { "X" };
    juce::TextButton deleteOverlayButton { "X" };
    juce::TextButton previewStreamButton { "PREVIEW" };  // Preview selected video clip
    
    // Preview window and component
    std::unique_ptr<juce::DialogWindow> previewWindow;
    std::unique_ptr<VideoPreviewComponent> previewComponent;
    
    juce::Label introHeaderLabel { {}, "Intro Clips" };
    juce::Label loopHeaderLabel { {}, "Loop Clips" };
    juce::Label overlayHeaderLabel { {}, "Overlay Clips" };
    
    juce::Label selectedFileLabel { {}, "No video selected" };
    
    // Preview area
    juce::Rectangle<int> previewArea;
    std::unique_ptr<juce::Image> currentThumbnail;
    std::unique_ptr<juce::Image> defaultThumbnail;
    std::unique_ptr<ThumbnailLoader> thumbnailLoader;
    
    // Editor components
    class DurationEditor : public juce::Label
    {
    public:
        DurationEditor(VideoPanel& owner, int clipIndex)
            : panel(owner), index(clipIndex)
        {
            setEditable(true, true, false);
            setJustificationType(juce::Justification::centred);
        }
        
        void textWasEdited() override
        {
            double value = getText().getDoubleValue();
            if (value > 0.1)
            {
                panel.updateClipDuration(index, value);
            }
        }
        
    private:
        VideoPanel& panel;
        int index;
    };
    
    class CrossfadeEditor : public juce::Label
    {
    public:
        CrossfadeEditor(VideoPanel& owner, int clipIndex)
            : panel(owner), index(clipIndex)
        {
            setEditable(true, true, false);
            setJustificationType(juce::Justification::centred);
        }
        
        void textWasEdited() override
        {
            double value = getText().getDoubleValue();
            if (value >= 0.0)
            {
                panel.updateClipCrossfade(index, value);
            }
        }
        
    private:
        VideoPanel& panel;
        int index;
    };
    
    class FrequencyEditor : public juce::Label
    {
    public:
        FrequencyEditor(VideoPanel& owner, int clipIndex)
            : panel(owner), index(clipIndex)
        {
            setEditable(true, true, false);
            setJustificationType(juce::Justification::centred);
        }
        
        void textWasEdited() override
        {
            double value = getText().getDoubleValue();
            if (value > 0.0)
            {
                panel.updateOverlayFrequency(index, value);
            }
        }
        
    private:
        VideoPanel& panel;
        int index;
    };
    
    class StartTimeEditor : public juce::Label
    {
    public:
        StartTimeEditor(VideoPanel& owner, int clipIndex)
            : panel(owner), index(clipIndex)
        {
            setEditable(true, true, false);
            setJustificationType(juce::Justification::centred);
        }
        
        void textWasEdited() override
        {
            double value = getText().getDoubleValue();
            if (value >= 0.0)
            {
                panel.updateOverlayStartTime(index, value);
            }
        }
        
    private:
        VideoPanel& panel;
        int index;
    };
    
    // Button container for up/down buttons in the table
    class ActionButtonsComponent : public juce::Component,
                                   private juce::Button::Listener
    {
    public:
        ActionButtonsComponent(VideoPanel& owner, int clipIndex, bool isIntro)
            : panel(owner), index(clipIndex), isIntroClip(isIntro), isOverlay(false)
        {
            upButton.setButtonText("Up");
            downButton.setButtonText("Down");
            
            upButton.addListener(this);
            downButton.addListener(this);
            
            addAndMakeVisible(upButton);
            addAndMakeVisible(downButton);
        }
        
        // Constructor for overlay clips
        ActionButtonsComponent(VideoPanel& owner, int clipIndex, bool isIntro, bool overlay)
            : panel(owner), index(clipIndex), isIntroClip(isIntro), isOverlay(overlay)
        {
            upButton.setButtonText("Up");
            downButton.setButtonText("Down");
            
            upButton.addListener(this);
            downButton.addListener(this);
            
            addAndMakeVisible(upButton);
            addAndMakeVisible(downButton);
        }
        
        void resized() override
        {
            auto area = getLocalBounds().reduced(2);
            // Make buttons taller to accommodate text instead of symbols
            upButton.setBounds(area.removeFromLeft(area.getWidth() / 2).reduced(1));
            downButton.setBounds(area.reduced(1));
        }
        
        void buttonClicked(juce::Button* button) override
        {
            if (isOverlay) {
                // Special case for overlay clips
                if (button == &upButton)
                    panel.moveClipUp(index, false); // isIntroClip=false will trigger overlay path
                else if (button == &downButton)
                    panel.moveClipDown(index, false); // isIntroClip=false will trigger overlay path
            } else {
                // Standard intro/loop clips
                if (button == &upButton)
                    panel.moveClipUp(index, isIntroClip);
                else if (button == &downButton)
                    panel.moveClipDown(index, isIntroClip);
            }
        }
        
    private:
        VideoPanel& panel;
        int index;
        bool isIntroClip;
        bool isOverlay;
        juce::TextButton upButton;
        juce::TextButton downButton;
    };
    
    // Methods to update clip properties
    void updateClipDuration(int clipIndex, double newDuration);
    void updateClipCrossfade(int clipIndex, double newCrossfade);
    void updateOverlayDuration(int clipIndex, double newDuration);
    void updateOverlayFrequency(int clipIndex, double newFrequency);
    void updateOverlayStartTime(int clipIndex, double startTimeSecs);
    
    // Get clips based on which table is active
    juce::Array<VideoClip>& getClipsForTable(bool isIntroTable)
    {
        return isIntroTable ? introClips : loopClips;
    }
    
    
    // Keep file choosers alive between function calls
    std::unique_ptr<juce::FileChooser> introVideoChooser;
    std::unique_ptr<juce::FileChooser> loopVideoChooser;
    std::unique_ptr<juce::FileChooser> overlayVideoChooser;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoPanel)
};
