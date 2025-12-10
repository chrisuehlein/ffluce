/*
  ==============================================================================

    BinauralTrackComponent.cpp
    Created: 18 Feb 2025 7:22:36pm
    Author:  chris

  ==============================================================================
*/
#include "BinauralTrackComponent.h"

BinauralTrackComponent::BinauralTrackComponent (BinauralAudioSource* source, BinauralAudioSource* streamingSource)
  : AudioTrackComponent ("Binaural Track"),
    binauralSource (source),
    streamingBinauralSource (streamingSource)
{
    // Configure frequency labels and editors
    leftLabel.setText("Left Hz", juce::dontSendNotification);
    leftLabel.setJustificationType (juce::Justification::centred);
    leftLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    juce::Font leftFont;
    leftFont.setHeight(12.0f);
    leftLabel.setFont(leftFont);
    addAndMakeVisible (leftLabel);

    rightLabel.setText("Right Hz", juce::dontSendNotification);
    rightLabel.setJustificationType (juce::Justification::centred);
    rightLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    juce::Font rightFont;
    rightFont.setHeight(12.0f);
    rightLabel.setFont(rightFont);
    addAndMakeVisible (rightLabel);
    
    // Configure auto-play button (commented out)
    // autoPlayButton.setColour(juce::ToggleButton::textColourId, juce::Colours::black);
    // autoPlayButton.setToggleState(false, juce::dontSendNotification);
    // autoPlayButton.onClick = [this]() { autoPlay = autoPlayButton.getToggleState(); };
    // addAndMakeVisible(autoPlayButton);

    leftEditor.setText ("70");
    leftEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    leftEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    leftEditor.setColour(juce::TextEditor::textColourId, juce::Colours::black);
    leftEditor.setColour(juce::CaretComponent::caretColourId, juce::Colours::black);
    leftEditor.setColour(juce::TextEditor::highlightColourId, juce::Colours::lightblue);
    leftEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    leftEditor.setInputRestrictions(5, "0123456789.");
    leftEditor.setJustification(juce::Justification::centred);
    leftEditor.addListener (this);
    addAndMakeVisible (leftEditor);
    
    // Apply text color directly
    leftEditor.applyColourToAllText(juce::Colours::black, true);

    rightEditor.setText ("74");
    rightEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    rightEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
    rightEditor.setColour(juce::TextEditor::textColourId, juce::Colours::black);
    rightEditor.setColour(juce::CaretComponent::caretColourId, juce::Colours::black);
    rightEditor.setColour(juce::TextEditor::highlightColourId, juce::Colours::lightblue);
    rightEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    rightEditor.setInputRestrictions(5, "0123456789.");
    rightEditor.setJustification(juce::Justification::centred);
    rightEditor.addListener (this);
    addAndMakeVisible (rightEditor);
    
    // Apply text color directly
    rightEditor.applyColourToAllText(juce::Colours::black, true);
    
    // Set initial values in the audio source
    if (binauralSource)
    {
        binauralSource->setLeftFrequency(70.0);
        binauralSource->setRightFrequency(74.0);
    }
    
    if (streamingBinauralSource)
    {
        streamingBinauralSource->setLeftFrequency(70.0);
        streamingBinauralSource->setRightFrequency(74.0);
    }
    
    // Set gain callback
    onGainChanged = [this](AudioTrackComponent*, float gain) {
        float actualGain = this->isMuted() ? 0.0f : gain;
        
        // Update UI audio source
        if (binauralSource) {
            binauralSource->setGain(actualGain);
        }
        
        // Update streaming audio source
        if (streamingBinauralSource) {
            streamingBinauralSource->setGain(actualGain);
        }
    };
}

void BinauralTrackComponent::resized()
{
    // Let the base class handle the title at top and fader/buttons at bottom
    AudioTrackComponent::resized();

    // Get the content area
    auto r = getLocalBounds().reduced(4);
    
    // Skip over title
    r.removeFromTop(25);
    
    // This is the area for our custom controls
    auto controlArea = r.removeFromTop(80); // Increased area for auto-play button
    
    // Auto-play button at the top (commented out)
    // autoPlayButton.setBounds(controlArea.removeFromTop(20).withSizeKeepingCentre(90, 20));
    
    // Space after auto-play button
    controlArea.removeFromTop(5);
    
    // Create a small area for each input
    int inputWidth = juce::jmin(50, controlArea.getWidth() / 2 - 15);
    int inputHeight = 20;
    
    // Center both frequency inputs horizontally with space between them
    int totalWidth = inputWidth * 2 + 20; // 20px gap between inputs
    auto centeredArea = controlArea.withSizeKeepingCentre(totalWidth, controlArea.getHeight());
    
    // Left frequency
    auto leftArea = centeredArea.removeFromLeft(inputWidth);
    leftLabel.setBounds(leftArea.removeFromTop(15));
    leftEditor.setBounds(leftArea.removeFromTop(inputHeight));
    
    // Gap between inputs
    centeredArea.removeFromLeft(20);
    
    // Right frequency
    auto rightArea = centeredArea;
    rightLabel.setBounds(rightArea.removeFromTop(15));
    rightEditor.setBounds(rightArea.removeFromTop(inputHeight));
}

void BinauralTrackComponent::textEditorFocusLost(juce::TextEditor& ed)
{
    updateFrequencies(ed);
}

void BinauralTrackComponent::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    updateFrequencies(ed);
}

void BinauralTrackComponent::setLeftFrequency(const juce::var& value)
{
    double freq = value;
    if (freq > 0.0)
    {
        leftEditor.setText(juce::String(freq), false);
        if (binauralSource)
            binauralSource->setLeftFrequency(freq);
        if (streamingBinauralSource)
            streamingBinauralSource->setLeftFrequency(freq);
    }
}

void BinauralTrackComponent::setRightFrequency(const juce::var& value)
{
    double freq = value;
    if (freq > 0.0)
    {
        rightEditor.setText(juce::String(freq), false);
        if (binauralSource)
            binauralSource->setRightFrequency(freq);
        if (streamingBinauralSource)
            streamingBinauralSource->setRightFrequency(freq);
    }
}

void BinauralTrackComponent::updateFrequencies(juce::TextEditor& ed)
{
    if (!binauralSource && !streamingBinauralSource)
        return;

    if (&ed == &leftEditor)
    {
        double freq = leftEditor.getText().getDoubleValue();
        if (freq > 0.0)
        {
            if (binauralSource)
                binauralSource->setLeftFrequency(freq);
            if (streamingBinauralSource)
                streamingBinauralSource->setLeftFrequency(freq);
        }
        else
        {
            // Reset to previous valid value
            leftEditor.setText("70", false);
        }
    }
    else if (&ed == &rightEditor)
    {
        double freq = rightEditor.getText().getDoubleValue();
        if (freq > 0.0)
        {
            if (binauralSource)
                binauralSource->setRightFrequency(freq);
            if (streamingBinauralSource)
                streamingBinauralSource->setRightFrequency(freq);
        }
        else
        {
            // Reset to previous valid value
            rightEditor.setText("74", false);
        }
    }
}
