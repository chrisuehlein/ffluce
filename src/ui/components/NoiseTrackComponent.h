#pragma once
#include <JuceHeader.h>
#include "AudioTrackComponent.h"
#include "../../audio/NoiseAudioSource.h"

/**
 * NoiseTrackComponent provides UI controls for the noise generator:
 * - Noise type selector (White/Pink/Brown)
 * - Volume fader with mute/solo (inherited from AudioTrackComponent)
 */
class NoiseTrackComponent : public AudioTrackComponent,
                           public juce::ComboBox::Listener
{
public:
    NoiseTrackComponent(NoiseAudioSource* source, NoiseAudioSource* streamingSource = nullptr);
    ~NoiseTrackComponent() override;

    void resized() override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    
    // Set the noise type and update UI
    void setNoiseType(NoiseAudioSource::NoiseType type);

private:
    NoiseAudioSource* noiseSource;
    NoiseAudioSource* streamingNoiseSource;
    juce::ComboBox noiseTypeCombo;
    juce::Label noiseTypeLabel;

    void updateNoiseSource();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseTrackComponent)
};