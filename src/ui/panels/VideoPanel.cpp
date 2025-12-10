/*
  ==============================================================================

    VideoPanel.cpp
    Created: 18 Feb 2025 7:24:30pm
    Author:  chris

  ==============================================================================
*/

#include "VideoPanel.h"
#include "VideoPreviewComponent.h"
#include <cstdlib> // for std::system

// Define our table models
class OverlayTableModel : public juce::TableListBoxModel
{
public:
    OverlayTableModel(VideoPanel& owner) : panel(owner) {}
    
    int getNumRows() override 
    { 
        return panel.getOverlayClips().size(); 
    }
    
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
        {
            // Green for overlay table
            g.fillAll(juce::Colour((juce::uint8)80, (juce::uint8)150, (juce::uint8)80, (juce::uint8)255));
        }
        else if (rowNumber % 2)
        {
            g.fillAll(juce::Colour((juce::uint8)60, (juce::uint8)60, (juce::uint8)60, (juce::uint8)255));
        }
    }
    
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override
    {
        g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
        juce::Font cellFont(14.0f);
        g.setFont(cellFont);
        
        auto& clips = panel.getOverlayClips();
        
        if (rowNumber < clips.size())
        {
            auto& clip = clips.getReference(rowNumber);
            juce::String text;
            
            switch (columnId)
            {
                case 1: // NameColumn
                    text = juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName();
                    break;
                case 2: // DurationColumn
                    text = juce::String(clip.duration, 1) + " sec";
                    break;
                case 3: // FrequencyColumn
                    text = juce::String(clip.frequencySecs, 1) + " sec";
                    break;
                case 4: // StartTimeColumn
                    text = juce::String(clip.startTimeSecs, 1) + " sec";
                    break;
            }
            
            g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft);
        }
    }
    
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override
    {
        if (columnId == 2) // DurationColumn
        {
            VideoPanel::DurationEditor* editor = dynamic_cast<VideoPanel::DurationEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::DurationEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getOverlayClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).duration, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 3) // FrequencyColumn
        {
            VideoPanel::FrequencyEditor* editor = dynamic_cast<VideoPanel::FrequencyEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::FrequencyEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getOverlayClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).frequencySecs, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 4) // StartTimeColumn
        {
            VideoPanel::StartTimeEditor* editor = dynamic_cast<VideoPanel::StartTimeEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::StartTimeEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getOverlayClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).startTimeSecs, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 5) // SequenceColumn (for moving clips up/down)
        {
            VideoPanel::ActionButtonsComponent* actionButtons = dynamic_cast<VideoPanel::ActionButtonsComponent*>(existingComponentToUpdate);
            
            if (actionButtons == nullptr)
            {
                actionButtons = new VideoPanel::ActionButtonsComponent(panel, rowNumber, false, true); // false for not intro, true for overlay
            }
            
            return actionButtons;
        }
        
        return nullptr;
    }
    
    void selectedRowsChanged(int lastRowSelected) override
    {
        panel.overlaySelectionChanged(lastRowSelected);
    }
    
private:
    VideoPanel& panel;
};

class IntroTableModel : public juce::TableListBoxModel
{
public:
    IntroTableModel(VideoPanel& owner) : panel(owner) {}
    
    int getNumRows() override 
    { 
        return panel.getIntroClips().size(); 
    }
    
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
        {
            // Blue for intro table
            g.fillAll(juce::Colour((juce::uint8)100, (juce::uint8)130, (juce::uint8)180, (juce::uint8)255));
        }
        else if (rowNumber % 2)
        {
            g.fillAll(juce::Colour((juce::uint8)60, (juce::uint8)60, (juce::uint8)60, (juce::uint8)255));
        }
    }
    
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override
    {
        g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
        juce::Font cellFont(14.0f);
        g.setFont(cellFont);
        
        auto& clips = panel.getIntroClips();
        
        if (rowNumber < clips.size())
        {
            auto& clip = clips.getReference(rowNumber);
            juce::String text;
            
            switch (columnId)
            {
                case 1: // NameColumn
                    text = juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName();
                    break;
                case 2: // DurationColumn
                    text = juce::String(clip.duration, 1) + " sec";
                    break;
                case 3: // CrossfadeColumn
                    text = juce::String(clip.crossfade, 1) + " sec";
                    break;
                case 4: // SequenceColumn
                    // Don't draw anything for sequence column
                    return;
            }
            
            g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft);
        }
    }
    
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override
    {
        if (columnId == 2) // DurationColumn
        {
            VideoPanel::DurationEditor* editor = dynamic_cast<VideoPanel::DurationEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::DurationEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getIntroClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).duration, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 3) // CrossfadeColumn
        {
            VideoPanel::CrossfadeEditor* editor = dynamic_cast<VideoPanel::CrossfadeEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::CrossfadeEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getIntroClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).crossfade, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 4) // SequenceColumn
        {
            VideoPanel::ActionButtonsComponent* actionButtons = dynamic_cast<VideoPanel::ActionButtonsComponent*>(existingComponentToUpdate);
            
            if (actionButtons == nullptr)
            {
                actionButtons = new VideoPanel::ActionButtonsComponent(panel, rowNumber, true); // true for intro
            }
            
            return actionButtons;
        }
        
        return nullptr;
    }
    
    void selectedRowsChanged(int lastRowSelected) override
    {
        panel.introSelectionChanged(lastRowSelected);
    }
    
private:
    VideoPanel& panel;
};

class LoopTableModel : public juce::TableListBoxModel
{
public:
    LoopTableModel(VideoPanel& owner) : panel(owner) {}
    
    int getNumRows() override 
    { 
        return panel.getLoopClips().size(); 
    }
    
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
        {
            // Green for loop table
            g.fillAll(juce::Colour((juce::uint8)100, (juce::uint8)180, (juce::uint8)130, (juce::uint8)255));
        }
        else if (rowNumber % 2)
        {
            g.fillAll(juce::Colour((juce::uint8)60, (juce::uint8)60, (juce::uint8)60, (juce::uint8)255));
        }
    }
    
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override
    {
        g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
        juce::Font cellFont(14.0f);
        g.setFont(cellFont);
        
        auto& clips = panel.getLoopClips();
        
        if (rowNumber < clips.size())
        {
            auto& clip = clips.getReference(rowNumber);
            juce::String text;
            
            switch (columnId)
            {
                case 1: // NameColumn
                    text = juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName();
                    break;
                case 2: // DurationColumn
                    text = juce::String(clip.duration, 1) + " sec";
                    break;
                case 3: // CrossfadeColumn
                    text = juce::String(clip.crossfade, 1) + " sec";
                    break;
                case 4: // SequenceColumn
                    // Don't draw anything for sequence column
                    return;
            }
            
            g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft);
        }
    }
    
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override
    {
        if (columnId == 2) // DurationColumn
        {
            VideoPanel::DurationEditor* editor = dynamic_cast<VideoPanel::DurationEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::DurationEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getLoopClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).duration, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 3) // CrossfadeColumn
        {
            VideoPanel::CrossfadeEditor* editor = dynamic_cast<VideoPanel::CrossfadeEditor*>(existingComponentToUpdate);
            
            if (editor == nullptr)
            {
                editor = new VideoPanel::CrossfadeEditor(panel, rowNumber);
            }
            
            auto& clips = panel.getLoopClips();
            if (rowNumber < clips.size())
            {
                editor->setText(juce::String(clips.getReference(rowNumber).crossfade, 1), juce::dontSendNotification);
            }
            
            return editor;
        }
        else if (columnId == 4) // SequenceColumn
        {
            VideoPanel::ActionButtonsComponent* actionButtons = dynamic_cast<VideoPanel::ActionButtonsComponent*>(existingComponentToUpdate);
            
            if (actionButtons == nullptr)
            {
                actionButtons = new VideoPanel::ActionButtonsComponent(panel, rowNumber, false); // false for loop
            }
            
            return actionButtons;
        }
        
        return nullptr;
    }
    
    void selectedRowsChanged(int lastRowSelected) override
    {
        panel.loopSelectionChanged(lastRowSelected);
    }
    
private:
    VideoPanel& panel;
};

std::unique_ptr<juce::Image> VideoPanel::createThumbnail(const juce::File& videoFile)
{
#if JUCE_WINDOWS
    // Create a unique temporary filename to avoid conflicts
    juce::String tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName();
    juce::String uniqueId = juce::String(juce::Random::getSystemRandom().nextInt(999999));
    juce::String thumbnailPath = tempDir + "\\thumbnail_" + uniqueId + ".jpg";
    
    // Get the application's executable directory to look for ffmpeg next to the .exe
    juce::String appPath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName();
    juce::String ffmpegExe = appPath + "\\ffmpeg.exe";
    
    juce::File ffmpegFile(ffmpegExe);
    if (!ffmpegFile.existsAsFile())
        ffmpegExe = "ffmpeg";
    
    // Use JUCE's ChildProcess to hide the console window
    juce::ChildProcess process;
    
    // Create command with proper flags to hide console
    // Use CharPointer_ASCII for string literals to prevent UTF-8 assertion failures
    juce::String ffmpegCmd = juce::String(juce::CharPointer_ASCII("\"")) + ffmpegExe + juce::String(juce::CharPointer_ASCII("\" -y -loglevel quiet -i \"")) + 
                          videoFile.getFullPathName() + juce::String(juce::CharPointer_ASCII("\" -vframes 1 -an -s 192x108 -ss 2 \"")) + 
                          thumbnailPath + juce::String(juce::CharPointer_ASCII("\""));
    
    bool success = process.start(ffmpegCmd, 0);

    if (success)
        process.waitForProcessToFinish(5000);
    
    // Create the file object after running the command
    juce::File thumbnailFile(thumbnailPath);
    
    // Give system time to complete I/O
    juce::Thread::sleep(100);
    
#else
    // Unix-based systems
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::String uniqueId = juce::String(juce::Random::getSystemRandom().nextInt(999999));
    juce::File thumbnailFile = tempDir.getChildFile("thumbnail_" + uniqueId + ".jpg");
    
    juce::String ffmpegCmd = "ffmpeg -y -i \"" + videoFile.getFullPathName() + 
                         "\" -vframes 1 -an -s 192x108 -ss 2 \"" + 
                         thumbnailFile.getFullPathName() + "\" 2>/dev/null";
                         
    juce::ChildProcess ffProcess;
    if (ffProcess.start(ffmpegCmd))
    {
        ffProcess.waitForProcessToFinish(5000);
    }
#endif
    
    if (thumbnailFile.existsAsFile())
    {
        try
        {
            juce::Image thumbnail = juce::ImageFileFormat::loadFrom(thumbnailFile);
            if (thumbnail.isValid())
                return std::make_unique<juce::Image>(thumbnail);
        }
        catch (const std::exception&)
        {
        }
    }

    // Create fallback image
    auto image = std::make_unique<juce::Image>(juce::Image::RGB, 192, 108, true);
    juce::Graphics g(*image);
    
    // Fill with dark background
    g.fillAll(juce::Colour(40, 40, 40));
    
    // Draw a border
    g.setColour(juce::Colours::white);
    g.drawRect(0, 0, 192, 108, 2);
    
    // Add a play icon
    g.setColour(juce::Colours::white);
    juce::Path playIcon;
    playIcon.addTriangle(76, 34, 76, 74, 126, 54);
    g.fillPath(playIcon);
    
    // Get filename and shorten if needed
    juce::String filename = videoFile.getFileName();
    if (filename.length() > 20)
        filename = filename.substring(0, 17) + "...";
    
    // Add the filename
    juce::Font font(12.0f, juce::Font::bold);
    g.setFont(font);
    g.drawText(filename, 
               juce::Rectangle<int>(5, 78, 182, 25),
               juce::Justification::centred, true);
    
    return image;
}

VideoPanel::VideoPanel()
    : introTable("IntroTable"), loopTable("LoopTable"), overlayTable("OverlayTable")
{
    // Configure intro table
    introTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(70, 70, 70));
    introTable.setOutlineThickness(1);
    
    // Create table models
    introModel = std::make_unique<IntroTableModel>(*this);
    loopModel = std::make_unique<LoopTableModel>(*this);
    overlayModel = std::make_unique<OverlayTableModel>(*this);
    
    // Create the thumbnail loader
    thumbnailLoader = std::make_unique<ThumbnailLoader>(*this);
    
    // Create default placeholder image
    defaultThumbnail = std::make_unique<juce::Image>(juce::Image::RGB, 192, 108, true);
    juce::Graphics g(*defaultThumbnail);
    g.fillAll(juce::Colour(40, 40, 40));
    g.setColour(juce::Colours::lightgrey);
    g.drawRect(0, 0, 192, 108, 1);
    g.setFont(juce::Font(14.0f));
    g.drawText("Loading...", juce::Rectangle<int>(0, 0, 192, 108), juce::Justification::centred);
    
    // Set models for tables
    introTable.setModel(introModel.get());
    loopTable.setModel(loopModel.get());
    overlayTable.setModel(overlayModel.get());
    
    // Start timer for UI updates
    startTimer(200);
    
    // Add columns to intro table
    auto& introHeader = introTable.getHeader();
    introHeader.addColumn("Name", 1, 180);
    introHeader.addColumn("Duration (s)", 2, 110);
    introHeader.addColumn("Xfade (s)", 3, 110);
    introHeader.addColumn("Sequence", 4, 120);
    
    // Disable sorting
    introHeader.setSortColumnId(0, false);
    
    introHeader.setStretchToFitActive(true);
    introTable.setMultipleSelectionEnabled(false);
    
    // Configure loop table
    loopTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(70, 70, 70));
    loopTable.setOutlineThickness(1);
    
    // Add columns to loop table
    auto& loopHeader = loopTable.getHeader();
    loopHeader.addColumn("Name", 1, 180);
    loopHeader.addColumn("Duration (s)", 2, 110);
    loopHeader.addColumn("Xfade (s)", 3, 110);
    loopHeader.addColumn("Sequence", 4, 120);
    
    // Disable sorting
    loopHeader.setSortColumnId(0, false);
    
    loopHeader.setStretchToFitActive(true);
    loopTable.setMultipleSelectionEnabled(false);
    
    // Configure overlay table
    overlayTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(70, 70, 70));
    overlayTable.setOutlineThickness(1);
    
    // Add columns to overlay table
    auto& overlayHeader = overlayTable.getHeader();
    overlayHeader.addColumn("Name", 1, 160);
    overlayHeader.addColumn("Duration (s)", 2, 100);
    overlayHeader.addColumn("Freq (s)", 3, 100); // Shorter text, in seconds
    overlayHeader.addColumn("Start With", 4, 90);
    overlayHeader.addColumn("Sequence", 5, 90);
    
    // Disable sorting
    overlayHeader.setSortColumnId(0, false);
    
    overlayHeader.setStretchToFitActive(true);
    overlayTable.setMultipleSelectionEnabled(false);

    // Add UI components
    addAndMakeVisible(introHeaderLabel);
    addAndMakeVisible(introTable);
    addAndMakeVisible(addIntroButton);
    addAndMakeVisible(deleteIntroButton);

    addAndMakeVisible(loopHeaderLabel);
    addAndMakeVisible(loopTable);
    addAndMakeVisible(addLoopButton);
    addAndMakeVisible(deleteLoopButton);
    
    addAndMakeVisible(overlayHeaderLabel);
    addAndMakeVisible(overlayTable);
    addAndMakeVisible(addOverlayButton);
    addAndMakeVisible(deleteOverlayButton);
    
    // Add preview/stream button (commented out for now)
    // addAndMakeVisible(previewStreamButton);
    // previewStreamButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 140, 200));
    // previewStreamButton.addListener(this);

    addAndMakeVisible(selectedFileLabel);

    // Set up button listeners
    addIntroButton.addListener(this);
    addLoopButton.addListener(this);
    addOverlayButton.addListener(this);
    deleteIntroButton.addListener(this);
    deleteLoopButton.addListener(this);
    deleteOverlayButton.addListener(this);

    // Configure labels
    introHeaderLabel.setJustificationType(juce::Justification::centredLeft);
    loopHeaderLabel.setJustificationType(juce::Justification::centredLeft);
    overlayHeaderLabel.setJustificationType(juce::Justification::centredLeft);
    introHeaderLabel.setText("Intro Clips (plays once at start)", juce::dontSendNotification);
    loopHeaderLabel.setText("Loop Clips (plays repeatedly)", juce::dontSendNotification);
    overlayHeaderLabel.setText("Overlay Clips (appears periodically)", juce::dontSendNotification);
    selectedFileLabel.setText("No video selected", juce::dontSendNotification);
    
    // Set initial active list
    activeList = IntroList;
}

void VideoPanel::resized()
{
    auto r = getLocalBounds().reduced(8);
    
    // Reserve space for preview on the right - use 16:9 aspect ratio
    previewArea = r.removeFromRight(192).withHeight(108);
    
    // Add buttons beside the preview area
    auto buttonArea = r.removeFromRight(40).withHeight(120);
    
    // File label below the preview
    selectedFileLabel.setBounds(previewArea.translated(0, previewArea.getHeight() + 5).withHeight(25));
    
    // Position preview/stream button overlapping the bottom of preview area (commented out)
    // auto previewButtonArea = previewArea.translated(0, previewArea.getHeight() - 5).withHeight(25);
    // previewStreamButton.setBounds(previewButtonArea);
    
    // Split the main area into three parts side by side
    int thirdWidth = r.getWidth() / 3;
    auto introArea = r.removeFromLeft(thirdWidth).reduced(3);
    auto loopArea = r.removeFromLeft(thirdWidth).reduced(3);
    auto overlayArea = r.reduced(3);
    
    // Layout intro section
    auto introHeaderRow = introArea.removeFromTop(30);
    introHeaderLabel.setBounds(introHeaderRow.removeFromLeft(introHeaderRow.getWidth() - 70));
    addIntroButton.setBounds(introHeaderRow.removeFromLeft(30).reduced(2));
    deleteIntroButton.setBounds(introHeaderRow.reduced(2));
    introTable.setBounds(introArea);
    
    // Layout loop section
    auto loopHeaderRow = loopArea.removeFromTop(30);
    loopHeaderLabel.setBounds(loopHeaderRow.removeFromLeft(loopHeaderRow.getWidth() - 70));
    addLoopButton.setBounds(loopHeaderRow.removeFromLeft(30).reduced(2));
    deleteLoopButton.setBounds(loopHeaderRow.reduced(2));
    loopTable.setBounds(loopArea);
    
    // Layout overlay section
    auto overlayHeaderRow = overlayArea.removeFromTop(30);
    overlayHeaderLabel.setBounds(overlayHeaderRow.removeFromLeft(overlayHeaderRow.getWidth() - 70));
    addOverlayButton.setBounds(overlayHeaderRow.removeFromLeft(30).reduced(2));
    deleteOverlayButton.setBounds(overlayHeaderRow.reduced(2));
    overlayTable.setBounds(overlayArea);
}

// Implementation of table row moving
void VideoPanel::moveClipUp(int index, bool isIntroClip)
{
    if (isIntroClip) {
        if (index > 0 && index < introClips.size()) {
            std::swap(introClips.getReference(index), introClips.getReference(index-1));
            introTable.updateContent();
            introTable.selectRow(index-1);
        }
    } else if (activeList == OverlayList) {
        // For overlay clips
        if (index > 0 && index < overlayClips.size()) {
            std::swap(overlayClips.getReference(index), overlayClips.getReference(index-1));
            overlayTable.updateContent();
            overlayTable.selectRow(index-1);
        }
    } else {
        // For loop clips
        if (index > 0 && index < loopClips.size()) {
            std::swap(loopClips.getReference(index), loopClips.getReference(index-1));
            loopTable.updateContent();
            loopTable.selectRow(index-1);
        }
    }
}

void VideoPanel::moveClipDown(int index, bool isIntroClip)
{
    if (isIntroClip) {
        if (index >= 0 && index < introClips.size()-1) {
            std::swap(introClips.getReference(index), introClips.getReference(index+1));
            introTable.updateContent();
            introTable.selectRow(index+1);
        }
    } else if (activeList == OverlayList) {
        // For overlay clips
        if (index >= 0 && index < overlayClips.size()-1) {
            std::swap(overlayClips.getReference(index), overlayClips.getReference(index+1));
            overlayTable.updateContent();
            overlayTable.selectRow(index+1);
        }
    } else {
        // For loop clips
        if (index >= 0 && index < loopClips.size()-1) {
            std::swap(loopClips.getReference(index), loopClips.getReference(index+1));
            loopTable.updateContent();
            loopTable.selectRow(index+1);
        }
    }
}

// Implementation of selection handlers
void VideoPanel::introSelectionChanged(int /*lastRowSelected*/)
{
    // Deselect the other tables when intro selection changes
    loopTable.deselectAllRows();
    overlayTable.deselectAllRows();
    
    // Set active list to intro
    activeList = IntroList;
    
    // Get the selected row
    int selectedRow = introTable.getSelectedRow();
    
    // Update preview if there's a selection
    if (selectedRow >= 0 && selectedRow < introClips.size()) {
        auto& clip = introClips.getReference(selectedRow);
        selectedFileLabel.setText("Selected: " + juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName(), 
                                juce::dontSendNotification);
        
        // Update thumbnail display
        if (clip.thumbnail)
            currentThumbnail = std::make_unique<juce::Image>(*clip.thumbnail);
        else if (!clip.isThumbnailLoading) {
            // Start loading if not already in progress
            clip.isThumbnailLoading = true;
            queueThumbnailGeneration(selectedRow, true, juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath));
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
        else {
            // Show default while loading
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
    } else {
        selectedFileLabel.setText("No video selected", juce::dontSendNotification);
        currentThumbnail.reset();
    }
    
    repaint(); // Update display
}

void VideoPanel::loopSelectionChanged(int /*lastRowSelected*/)
{
    // Deselect the other tables when loop selection changes
    introTable.deselectAllRows();
    overlayTable.deselectAllRows();
    
    // Set active list to loop
    activeList = LoopList;
    
    // Get the selected row
    int selectedRow = loopTable.getSelectedRow();
    
    // Update preview if there's a selection
    if (selectedRow >= 0 && selectedRow < loopClips.size()) {
        auto& clip = loopClips.getReference(selectedRow);
        selectedFileLabel.setText("Selected: " + juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName(), 
                                juce::dontSendNotification);
        
        // Update thumbnail display
        if (clip.thumbnail)
            currentThumbnail = std::make_unique<juce::Image>(*clip.thumbnail);
        else if (!clip.isThumbnailLoading) {
            // Start loading if not already in progress
            clip.isThumbnailLoading = true;
            queueThumbnailGeneration(selectedRow, false, juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath));
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
        else {
            // Show default while loading
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
    } else {
        selectedFileLabel.setText("No video selected", juce::dontSendNotification);
        currentThumbnail.reset();
    }
    
    repaint(); // Update display
}

void VideoPanel::overlaySelectionChanged(int /*lastRowSelected*/)
{
    // Deselect the other tables when overlay selection changes
    introTable.deselectAllRows();
    loopTable.deselectAllRows();
    
    // Set active list to overlay
    activeList = OverlayList;
    
    // Get the selected row
    int selectedRow = overlayTable.getSelectedRow();
    
    // Update preview if there's a selection
    if (selectedRow >= 0 && selectedRow < overlayClips.size()) {
        auto& clip = overlayClips.getReference(selectedRow);
        selectedFileLabel.setText("Selected Overlay: " + juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath).getFileName(), 
                                juce::dontSendNotification);
        
        // Update thumbnail display
        if (clip.thumbnail)
            currentThumbnail = std::make_unique<juce::Image>(*clip.thumbnail);
        else if (!clip.isThumbnailLoading) {
            // Start loading if not already in progress
            clip.isThumbnailLoading = true;
            queueThumbnailGeneration(selectedRow, false, juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath));
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
        else {
            // Show default while loading
            currentThumbnail = defaultThumbnail ? std::make_unique<juce::Image>(*defaultThumbnail) : nullptr;
        }
    } else {
        selectedFileLabel.setText("No overlay selected", juce::dontSendNotification);
        currentThumbnail.reset();
    }
    
    repaint(); // Update display
}

// Methods to update clip properties
void VideoPanel::updateClipDuration(int clipIndex, double newDuration)
{
    // Determine which table the edit is coming from
    juce::Component* comp = juce::Component::getCurrentlyFocusedComponent();
    bool isIntroTable = (comp == &introTable || introTable.isParentOf(comp));
    
    // Update the appropriate table
    if (isIntroTable)
    {
        if (clipIndex >= 0 && clipIndex < introClips.size())
        {
            introClips.getReference(clipIndex).duration = newDuration;
            introTable.repaintRow(clipIndex);
        }
    }
    else // Loop table
    {
        if (clipIndex >= 0 && clipIndex < loopClips.size())
        {
            loopClips.getReference(clipIndex).duration = newDuration;
            loopTable.repaintRow(clipIndex);
        }
    }
}

void VideoPanel::updateClipCrossfade(int clipIndex, double newCrossfade)
{
    // Determine which table the edit is coming from
    juce::Component* comp = juce::Component::getCurrentlyFocusedComponent();
    bool isIntroTable = (comp == &introTable || introTable.isParentOf(comp));
    
    // Update the appropriate table
    if (isIntroTable)
    {
        if (clipIndex >= 0 && clipIndex < introClips.size())
        {
            introClips.getReference(clipIndex).crossfade = newCrossfade;
            introTable.repaintRow(clipIndex);
        }
    }
    else // Loop table
    {
        if (clipIndex >= 0 && clipIndex < loopClips.size())
        {
            loopClips.getReference(clipIndex).crossfade = newCrossfade;
            loopTable.repaintRow(clipIndex);
        }
    }
}

void VideoPanel::updateOverlayDuration(int clipIndex, double newDuration)
{
    if (clipIndex >= 0 && clipIndex < overlayClips.size())
    {
        overlayClips.getReference(clipIndex).duration = newDuration;
        overlayTable.repaintRow(clipIndex);
    }
}

void VideoPanel::updateOverlayFrequency(int clipIndex, double newFrequency)
{
    if (clipIndex >= 0 && clipIndex < overlayClips.size())
    {
        overlayClips.getReference(clipIndex).frequencySecs = newFrequency;
        overlayTable.repaintRow(clipIndex);
    }
}

void VideoPanel::updateOverlayStartTime(int clipIndex, double startTimeSecs)
{
    if (clipIndex >= 0 && clipIndex < overlayClips.size())
    {
        overlayClips.getReference(clipIndex).startTimeSecs = startTimeSecs;
        overlayTable.repaintRow(clipIndex);
    }
}

// Add clip with specified duration and crossfade
void VideoPanel::addIntroClip(const juce::File& videoFile, double duration, double crossfade)
{
    // Make sure the file exists
    if (!videoFile.existsAsFile())
        return;
        
    // Set active list to intro
    activeList = IntroList;
    
    // Create new clip
    VideoClip clip;
    clip.filePath = videoFile.getFullPathName();
    
    // Always get native duration first
    double nativeDuration = getVideoDuration(videoFile);
    
    // Use native duration if requested duration is negative or zero
    if (duration <= 0.0)
    {
        clip.duration = nativeDuration;
    }
    else
    {
        clip.duration = duration;
    }
    
    clip.crossfade = crossfade;
    
    // Add to intro clips
    introClips.add(clip);
    introTable.updateContent();
    
    // Queue thumbnail generation in background - will happen via timer callback
    
    // Explicitly deselect the other table
    loopTable.deselectAllRows();
    
    // Select the new clip
    introTable.selectRow(introClips.size() - 1);
}

void VideoPanel::addLoopClip(const juce::File& videoFile, double duration, double crossfade)
{
    // Make sure the file exists
    if (!videoFile.existsAsFile())
        return;
        
    // Set active list to loop
    activeList = LoopList;
    
    // Create new clip
    VideoClip clip;
    clip.filePath = videoFile.getFullPathName();
    
    // Always get native duration first
    double nativeDuration = getVideoDuration(videoFile);
    
    // Use native duration if requested duration is negative or zero
    if (duration <= 0.0)
    {
        clip.duration = nativeDuration;
    }
    else
    {
        clip.duration = duration;
    }
    
    clip.crossfade = crossfade;
    
    // Add to loop clips
    loopClips.add(clip);
    loopTable.updateContent();
    
    // Explicitly deselect the other tables
    introTable.deselectAllRows();
    overlayTable.deselectAllRows();
    
    // Select the new clip
    loopTable.selectRow(loopClips.size() - 1);
}

void VideoPanel::addOverlayClip(const juce::File& videoFile, double duration, double frequencySecs, double startTimeSecs)
{
    // Make sure the file exists
    if (!videoFile.existsAsFile())
        return;
        
    // Set active list to overlay
    activeList = OverlayList;
    
    // Create new clip
    OverlayClip clip;
    clip.filePath = videoFile.getFullPathName();
    
    // Check if this is an image file (PNG, JPG, etc.)
    juce::String ext = videoFile.getFileExtension().toLowerCase();
    bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif");
    
    if (isImage) {
        clip.duration = (duration <= 0.0) ? 5.0 : duration;
    } else {
        // For videos, get the native duration first
        double nativeDuration = getVideoDuration(videoFile);
        
        // Use native duration if requested duration is negative or zero
        if (duration <= 0.0) {
            clip.duration = nativeDuration;
        } else {
            clip.duration = duration;
        }
    }
    
    clip.frequencySecs = frequencySecs;
    clip.startTimeSecs = startTimeSecs;
    
    // Add to overlay clips
    overlayClips.add(clip);
    overlayTable.updateContent();
    
    // Explicitly deselect the other tables
    introTable.deselectAllRows();
    loopTable.deselectAllRows();
    
    // Select the new clip
    overlayTable.selectRow(overlayClips.size() - 1);
}

// Get clip data for saving
VideoPanel::ClipData VideoPanel::getIntroClipData(int index) const
{
    ClipData data;
    
    if (index >= 0 && index < introClips.size())
    {
        const auto& clip = introClips.getReference(index);
        data.filePath = clip.filePath;
        data.duration = clip.duration;
        data.crossfade = clip.crossfade;
    }
    
    return data;
}

VideoPanel::ClipData VideoPanel::getLoopClipData(int index) const
{
    ClipData data;
    
    if (index >= 0 && index < loopClips.size())
    {
        const auto& clip = loopClips.getReference(index);
        data.filePath = clip.filePath;
        data.duration = clip.duration;
        data.crossfade = clip.crossfade;
    }
    
    return data;
}

VideoPanel::OverlayClipData VideoPanel::getOverlayClipData(int index) const
{
    OverlayClipData data;
    
    if (index >= 0 && index < overlayClips.size())
    {
        const auto& clip = overlayClips.getReference(index);
        data.filePath = clip.filePath;
        data.duration = clip.duration;
        data.frequencySecs = clip.frequencySecs;
        data.startTimeSecs = clip.startTimeSecs;
    }
    
    return data;
}

double VideoPanel::getVideoDuration(const juce::File& videoFile)
{
    double defaultDuration = 15.0;

#if JUCE_WINDOWS
    // Get the application's executable directory to look for ffprobe next to the .exe
    juce::String appPath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName();
    juce::String ffprobeExe = appPath + "\\ffprobe.exe";
    
    juce::File ffprobeFile(ffprobeExe);
    if (!ffprobeFile.existsAsFile())
        ffprobeExe = "ffprobe";

    juce::ChildProcess process;
    juce::String ffprobeCmd = juce::String(juce::CharPointer_ASCII("\"")) + ffprobeExe +
                           juce::String(juce::CharPointer_ASCII("\" -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 -i \"")) +
                           videoFile.getFullPathName() + juce::String(juce::CharPointer_ASCII("\""));

    if (process.start(ffprobeCmd, juce::ChildProcess::wantStdOut))
    {
        process.waitForProcessToFinish(5000);
        juce::String output = process.readAllProcessOutput().trim();
        double duration = output.getDoubleValue();
        if (duration > 0.1)
            return duration;
    }
#else
    juce::ChildProcess process;
    juce::String ffprobeCmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 -i \"" +
                            videoFile.getFullPathName() + "\"";

    if (process.start(ffprobeCmd, juce::ChildProcess::wantStdOut))
    {
        process.waitForProcessToFinish(5000);
        juce::String output = process.readAllProcessOutput().trim();
        double duration = output.getDoubleValue();
        if (duration > 0.1)
            return duration;
    }
#endif
    
    // Fallback - try JUCE's built-in functionality
    if (videoFile.existsAsFile())
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(videoFile));
        if (reader != nullptr && reader->sampleRate > 0 && reader->lengthInSamples > 0)
            return static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }

    return defaultDuration;
}

VideoPanel::~VideoPanel()
{
    // Stop the timer
    stopTimer();
    
    // Shut down the thumbnail loader thread
    if (thumbnailLoader)
        thumbnailLoader->stopThread(1000);
    
    // Close any open preview window
    if (previewWindow != nullptr)
    {
        // Stop any ongoing previews
        if (auto* content = dynamic_cast<VideoPreviewComponent*>(previewWindow->getContentComponent()))
        {
            content->stopPreview();
            content->stopStreaming();
        }
        
        previewWindow->setVisible(false);
        previewWindow.reset();
    }
    
    // Remove mouse listeners
    introTable.removeMouseListener(this);
    loopTable.removeMouseListener(this);
}

void VideoPanel::paint(juce::Graphics& g)
{
    // Draw the preview area background
    g.setColour(juce::Colour(40, 40, 40));
    g.fillRect(previewArea);
    
    g.setColour(juce::Colours::white);
    g.drawRect(previewArea, 1);
    
    // Draw the thumbnail if it exists
    if (currentThumbnail != nullptr)
    {
        g.drawImageWithin(*currentThumbnail, 
                          previewArea.getX() + 2,
                          previewArea.getY() + 2,
                          previewArea.getWidth() - 4,
                          previewArea.getHeight() - 4,
                          juce::RectanglePlacement::centred);
    }
    else if (defaultThumbnail != nullptr)
    {
        // Draw placeholder image while loading
        g.drawImageWithin(*defaultThumbnail, 
                          previewArea.getX() + 2,
                          previewArea.getY() + 2,
                          previewArea.getWidth() - 4,
                          previewArea.getHeight() - 4,
                          juce::RectanglePlacement::centred);
    }
    else
    {
        g.setColour(juce::Colours::grey);
        juce::Font previewFont(16.0f);
        g.setFont(previewFont);
        g.drawText("No preview available", 
                   previewArea, 
                   juce::Justification::centred);
    }
}

void VideoPanel::timerCallback()
{
    if (previewWindow != nullptr && !previewWindow->isVisible())
        previewWindow.reset();
    
    // Check intro clips
    for (int i = 0; i < introClips.size(); ++i)
    {
        auto& clip = introClips.getReference(i);
        if (!clip.thumbnailLoaded && !clip.isThumbnailLoading)
        {
            // Mark as loading
            clip.isThumbnailLoading = true;
            
            // Queue loading in background
            queueThumbnailGeneration(i, true, juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath));
            break; // Only start one at a time to avoid overwhelming the system
        }
    }
    
    // Check loop clips
    for (int i = 0; i < loopClips.size(); ++i)
    {
        auto& clip = loopClips.getReference(i);
        if (!clip.thumbnailLoaded && !clip.isThumbnailLoading)
        {
            // Mark as loading
            clip.isThumbnailLoading = true;
            
            // Queue loading in background
            queueThumbnailGeneration(i, false, juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath));
            break; // Only start one at a time
        }
    }
    
    // Check overlay clips - special case for overlays
    for (int i = 0; i < overlayClips.size(); ++i)
    {
        auto& clip = overlayClips.getReference(i);
        if (!clip.thumbnailLoaded && !clip.isThumbnailLoading)
        {
            // Mark as loading
            clip.isThumbnailLoading = true;
            
            // Generate the thumbnail directly rather than using the complex loader
            // This avoids issues with the duration detection
            juce::MessageManager::callAsync([this, i, file = juce::File::getCurrentWorkingDirectory().getChildFile(clip.filePath)]() {
                auto thumbnail = createThumbnail(file);
                this->setThumbnailForOverlay(i, std::move(thumbnail));
            });
            
            break; // Only start one at a time
        }
    }
}

void VideoPanel::queueThumbnailGeneration(int clipIndex, bool isIntro, const juce::File& videoFile)
{
    if (!videoFile.existsAsFile())
        return;

    thumbnailLoader->loadThumbnail(clipIndex, isIntro, videoFile);
}

void VideoPanel::setThumbnailForClip(int clipIndex, bool isIntro, std::unique_ptr<juce::Image> thumbnail)
{
    if (isIntro)
    {
        if (clipIndex >= 0 && clipIndex < introClips.size())
        {
            auto& clip = introClips.getReference(clipIndex);
            clip.thumbnail = std::move(thumbnail);
            clip.thumbnailLoaded = true;
            
            // Update display if this is the current selection
            if (introTable.getSelectedRow() == clipIndex)
            {
                currentThumbnail = clip.thumbnail ? std::make_unique<juce::Image>(*clip.thumbnail) : nullptr;
                repaint();
            }
        }
    }
    else
    {
        if (clipIndex >= 0 && clipIndex < loopClips.size())
        {
            auto& clip = loopClips.getReference(clipIndex);
            clip.thumbnail = std::move(thumbnail);
            clip.thumbnailLoaded = true;
            
            // Update display if this is the current selection
            if (loopTable.getSelectedRow() == clipIndex)
            {
                currentThumbnail = clip.thumbnail ? std::make_unique<juce::Image>(*clip.thumbnail) : nullptr;
                repaint();
            }
        }
    }
}

void VideoPanel::setThumbnailForOverlay(int clipIndex, std::unique_ptr<juce::Image> thumbnail)
{
    if (clipIndex >= 0 && clipIndex < overlayClips.size())
    {
        auto& clip = overlayClips.getReference(clipIndex);
        clip.thumbnail = std::move(thumbnail);
        clip.thumbnailLoaded = true;
        
        // Update display if this is the current selection
        if (overlayTable.getSelectedRow() == clipIndex)
        {
            currentThumbnail = clip.thumbnail ? std::make_unique<juce::Image>(*clip.thumbnail) : nullptr;
            repaint();
        }
    }
}

void VideoPanel::buttonClicked(juce::Button* b)
{
    if (b == &addIntroButton)
    {
        // Create a file chooser and keep it alive
        introVideoChooser.reset(new juce::FileChooser("Select Intro Video",
                                  juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                  "*.mp4;*.mov;*.avi;*.mkv"));
        
        // Use async method and keep object alive with class member
        introVideoChooser->launchAsync(juce::FileBrowserComponent::openMode | 
                             juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
        {
            juce::File file = chooser.getResult();
            if (file.existsAsFile())
            {
                    // Deselect any selection in the other tables first
                    loopTable.deselectAllRows();
                    overlayTable.deselectAllRows();
                    
                    // Set list as active now
                    activeList = IntroList;
                    
                    // Add to intro clips using helper method with duration = 0 to use native duration
                    addIntroClip(file, 0.0, 1.0);
            }
        });
    }
    else if (b == &addLoopButton)
    {
        // Create a file chooser and keep it alive
        loopVideoChooser.reset(new juce::FileChooser("Select Loop Video",
                                  juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                  "*.mp4;*.mov;*.avi;*.mkv"));
        
        // Use async method and keep object alive with class member
        loopVideoChooser->launchAsync(juce::FileBrowserComponent::openMode | 
                             juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
        {
            juce::File file = chooser.getResult();
            if (file.existsAsFile())
            {
                    // Deselect any selection in the other tables first
                    introTable.deselectAllRows();
                    overlayTable.deselectAllRows();
                    
                    // Set list as active now
                    activeList = LoopList;
                    
                    // Add to loop clips using helper method with duration = 0 to use native duration
                    addLoopClip(file, 0.0, 1.0);
            }
        });
    }
    else if (b == &addOverlayButton)
    {
        // Create a file chooser and keep it alive - include all common image formats for static overlays
        overlayVideoChooser.reset(new juce::FileChooser("Select Overlay Video or Image",
                                  juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                  "*.mp4;*.mov;*.avi;*.mkv;*.png;*.jpg;*.jpeg;*.bmp;*.gif"));
        
        // Use async method and keep object alive with class member
        overlayVideoChooser->launchAsync(juce::FileBrowserComponent::openMode | 
                             juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
        {
            juce::File file = chooser.getResult();
            if (file.existsAsFile())
            {
                    // Deselect any selection in the other tables first
                    introTable.deselectAllRows();
                    loopTable.deselectAllRows();
                    
                    // Set list as active now
                    activeList = OverlayList;
                    
                    // Add to overlay clips using helper method
                    // Default frequency: 5 minutes, start time 10 seconds in
                    addOverlayClip(file, 0.0, 5.0, 10.0);
            }
        });
    }
    else if (b == &deleteIntroButton)
    {
        int selectedRow = introTable.getSelectedRow();
        if (selectedRow >= 0 && selectedRow < introClips.size())
        {
            introClips.remove(selectedRow);
            introTable.updateContent();
            currentThumbnail.reset();
            selectedFileLabel.setText("No video selected", juce::dontSendNotification);
            repaint();
        }
    }
    else if (b == &deleteLoopButton)
    {
        int selectedRow = loopTable.getSelectedRow();
        if (selectedRow >= 0 && selectedRow < loopClips.size())
        {
            loopClips.remove(selectedRow);
            loopTable.updateContent();
            currentThumbnail.reset();
            selectedFileLabel.setText("No video selected", juce::dontSendNotification);
            repaint();
        }
    }
    else if (b == &deleteOverlayButton)
    {
        int selectedRow = overlayTable.getSelectedRow();
        if (selectedRow >= 0 && selectedRow < overlayClips.size())
        {
            overlayClips.remove(selectedRow);
            overlayTable.updateContent();
            currentThumbnail.reset();
            selectedFileLabel.setText("No video selected", juce::dontSendNotification);
            repaint();
        }
    }
    else if (b == &previewStreamButton)
    {
        // Launch ffplay for the currently selected video clip
        juce::String selectedFilePath = selectedFileLabel.getText();
        
        if (selectedFilePath.isEmpty() || selectedFilePath == "No video selected")
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "No Video Selected",
                "Please select a video clip to preview.",
                "OK"
            );
            return;
        }
        
        // Launch ffplay with loop option
        juce::String ffplayCommand = "ffplay -loop 0 \"" + selectedFilePath + "\"";
        
        juce::ChildProcess ffplayProcess;
        if (!ffplayProcess.start(ffplayCommand))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Preview Failed",
                "Could not launch ffplay. Make sure ffmpeg is installed and in your PATH.",
                "OK"
            );
        }
    }
}

std::vector<RenderTypes::VideoClipInfo> VideoPanel::getIntroClipsForStreaming() const
{
    std::vector<RenderTypes::VideoClipInfo> result;
    result.reserve(introClips.size());
    
    for (const auto& clip : introClips)
    {
        RenderTypes::VideoClipInfo info;
        info.file = juce::File(clip.filePath);
        info.startTime = 0.0;
        info.duration = clip.duration;
        info.crossfade = clip.crossfade;
        info.isIntroClip = true;
        result.push_back(info);
    }
    
    return result;
}

std::vector<RenderTypes::VideoClipInfo> VideoPanel::getLoopClipsForStreaming() const
{
    std::vector<RenderTypes::VideoClipInfo> result;
    result.reserve(loopClips.size());
    
    for (const auto& clip : loopClips)
    {
        RenderTypes::VideoClipInfo info;
        info.file = juce::File(clip.filePath);
        info.startTime = 0.0;
        info.duration = clip.duration;
        info.crossfade = clip.crossfade;
        info.isIntroClip = false;
        result.push_back(info);
    }
    
    return result;
}

std::vector<RenderTypes::OverlayClipInfo> VideoPanel::getOverlayClipsForStreaming() const
{
    std::vector<RenderTypes::OverlayClipInfo> result;
    result.reserve(overlayClips.size());
    
    for (const auto& clip : overlayClips)
    {
        RenderTypes::OverlayClipInfo info;
        info.file = juce::File(clip.filePath);
        info.duration = clip.duration;
        info.frequencySecs = clip.frequencySecs;
        info.startTimeSecs = clip.startTimeSecs;
        result.push_back(info);
    }
    
    return result;
}
