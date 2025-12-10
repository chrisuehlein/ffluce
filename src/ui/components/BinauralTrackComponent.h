#pragma once
#include "AudioTrackComponent.h"
#include "../../audio/BinauralAudioSource.h"

/**
    BinauralTrackComponent:
      - Inherits track UI with vertical fader
      - Adds left/right frequency text editors
      - Proper gain control connected to audio source
*/
class BinauralTrackComponent : public AudioTrackComponent,
    private juce::TextEditor::Listener
{
public:
    BinauralTrackComponent(BinauralAudioSource* source, BinauralAudioSource* streamingSource = nullptr);
    void resized() override;
    
    // Methods for project save/load
    double getLeftFrequency() const { return leftEditor.getText().getDoubleValue(); }
    double getRightFrequency() const { return rightEditor.getText().getDoubleValue(); }
    
    void setLeftFrequency(const juce::var& value);
    void setRightFrequency(const juce::var& value);
    
    // Auto-play control (commented out)
    // bool shouldAutoPlay() const { return autoPlay; }
    // void setShouldAutoPlay(bool shouldPlay) 
    // { 
    //     autoPlay = shouldPlay; 
    //     autoPlayButton.setToggleState(shouldPlay, juce::dontSendNotification);
    // }

private:
    void textEditorFocusLost(juce::TextEditor& ed) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void updateFrequencies(juce::TextEditor& ed);

    BinauralAudioSource* binauralSource = nullptr;
    BinauralAudioSource* streamingBinauralSource = nullptr;

    juce::Label leftLabel;
    juce::Label rightLabel;
    juce::TextEditor leftEditor, rightEditor;
    
    // juce::ToggleButton autoPlayButton {"Auto-play"};
    // bool autoPlay = false;
};
