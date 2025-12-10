#include "NoiseTrackComponent.h"

NoiseTrackComponent::NoiseTrackComponent(NoiseAudioSource* source, NoiseAudioSource* streamingSource)
    : AudioTrackComponent("Noise Generator"), noiseSource(source), streamingNoiseSource(streamingSource)
{
    // Set up noise type selector
    noiseTypeLabel.setText("Type:", juce::dontSendNotification);
    noiseTypeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(noiseTypeLabel);

    noiseTypeCombo.addItem("White Noise", 1);
    noiseTypeCombo.addItem("Pink Noise", 2);
    noiseTypeCombo.addItem("Brown Noise", 3);
    noiseTypeCombo.setSelectedId(1); // Default to white noise
    noiseTypeCombo.addListener(this);
    addAndMakeVisible(noiseTypeCombo);

    // Set up gain callback to update both noise sources
    onGainChanged = [this](AudioTrackComponent* track, float gain) {
        bool muted = track->isMuted();
        
        // Update UI noise source
        if (noiseSource) {
            noiseSource->setGain(gain);
            noiseSource->setMuted(muted);
        }
        
        // Update streaming noise source
        if (streamingNoiseSource) {
            streamingNoiseSource->setGain(gain);
            streamingNoiseSource->setMuted(muted);
        }
    };

    // Initialize noise source with current settings
    updateNoiseSource();
}

NoiseTrackComponent::~NoiseTrackComponent()
{
}

void NoiseTrackComponent::resized()
{
    AudioTrackComponent::resized();
    
    // Use the controls area that the base class reserved
    auto r = getLocalBounds().reduced(4);
    r.removeFromTop(25); // Skip title area
    
    // This is the 50px controls area reserved by the base class
    auto controlsArea = r.removeFromTop(50);
    
    // Arrange label and combo box
    auto labelArea = controlsArea.removeFromTop(20);
    noiseTypeLabel.setBounds(labelArea.removeFromLeft(50));
    
    auto comboArea = controlsArea.removeFromTop(25);
    noiseTypeCombo.setBounds(comboArea.reduced(2));
}

void NoiseTrackComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &noiseTypeCombo)
    {
        updateNoiseSource();
    }
}

void NoiseTrackComponent::updateNoiseSource()
{
    // Update noise type based on combo box selection
    int selectedId = noiseTypeCombo.getSelectedId();
    NoiseAudioSource::NoiseType type = NoiseAudioSource::White;
    
    switch (selectedId)
    {
        case 1: type = NoiseAudioSource::White; break;
        case 2: type = NoiseAudioSource::Pink; break;
        case 3: type = NoiseAudioSource::Brown; break;
    }
    
    // Update UI noise source
    if (noiseSource) {
        noiseSource->setNoiseType(type);
        noiseSource->setGain(getActualGain());
        noiseSource->setMuted(isMuted());
    }
    
    // Update streaming noise source
    if (streamingNoiseSource) {
        streamingNoiseSource->setNoiseType(type);
        streamingNoiseSource->setGain(getActualGain());
        streamingNoiseSource->setMuted(isMuted());
    }
}

void NoiseTrackComponent::setNoiseType(NoiseAudioSource::NoiseType type)
{
    // Update combo box to match noise type
    int selectedId = 1;
    switch (type)
    {
        case NoiseAudioSource::White: selectedId = 1; break;
        case NoiseAudioSource::Pink: selectedId = 2; break;
        case NoiseAudioSource::Brown: selectedId = 3; break;
    }
    
    noiseTypeCombo.setSelectedId(selectedId, juce::dontSendNotification);
    
    // Update the noise source
    if (noiseSource)
        noiseSource->setNoiseType(type);
}