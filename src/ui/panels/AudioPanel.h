#pragma once

#include <JuceHeader.h>
#include "../../audio/BinauralAudioSource.h"
#include "../../audio/FilePlayerAudioSource.h"
#include "../../audio/NoiseAudioSource.h"
#include "../components/BinauralTrackComponent.h"
#include "../components/FileTrackComponent.h"
#include "../components/NoiseTrackComponent.h"

/**
 * AudioPanel:
 * - Contains three track components (binaural, file player, and noise generator)
 * - Arranges them side by side with equal width
 * - Handles solo synchronization between tracks
 */
class AudioPanel : public juce::Component
{
public:
    AudioPanel() : backgroundColour(juce::Colours::transparentBlack) {} // Transparent background
    ~AudioPanel() override {}

    void setSources(BinauralAudioSource* binSrc, FilePlayerAudioSource* fileSrc, NoiseAudioSource* noiseSrc,
                   BinauralAudioSource* streamingBinSrc = nullptr, FilePlayerAudioSource* streamingFileSrc = nullptr, NoiseAudioSource* streamingNoiseSrc = nullptr)
    {
        binauralSource = binSrc;
        filePlayer = fileSrc;
        noiseSource = noiseSrc;

        // Create UI track components
        binauralTrack = std::make_unique<BinauralTrackComponent>(binauralSource, streamingBinSrc);
        fileTrack = std::make_unique<FileTrackComponent>(filePlayer, streamingFileSrc);
        noiseTrack = std::make_unique<NoiseTrackComponent>(noiseSource, streamingNoiseSrc);

        // Setup solo callbacks
        binauralTrack->onSoloChanged = [this](AudioTrackComponent* track) {
            handleSoloChanged(track);
        };
        
        fileTrack->onSoloChanged = [this](AudioTrackComponent* track) {
            handleSoloChanged(track);
        };
        
        noiseTrack->onSoloChanged = [this](AudioTrackComponent* track) {
            handleSoloChanged(track);
        };

        addAndMakeVisible(binauralTrack.get());
        addAndMakeVisible(fileTrack.get());
        addAndMakeVisible(noiseTrack.get());
    }

    void paint(juce::Graphics& g) override
    {
        // Don't fill the background - let it be transparent
        // Only draw the separator line between tracks
        g.setColour(juce::Colours::grey);
        g.fillRect(separatorBounds);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        
        const int meterWidth = 30;
        const int spacing = 8;
        const int meterCount = 3;

        int fileTrackMin = 420;
        int binauralTrackMin = 180;
        int noiseTrackMin = 180;

        int requiredWidth = fileTrackMin + binauralTrackMin + noiseTrackMin + meterWidth * meterCount + spacing * 2;
        int availableWidth = area.getWidth();

        int fileTrackWidth = fileTrackMin;
        int binauralTrackWidth = binauralTrackMin;
        int noiseTrackWidth = noiseTrackMin;

        if (availableWidth < requiredWidth)
        {
            int deficit = requiredWidth - availableWidth;
            int reduce = deficit / 3;
            fileTrackWidth = juce::jmax(220, fileTrackWidth - reduce);
            binauralTrackWidth = juce::jmax(150, binauralTrackWidth - reduce);
            noiseTrackWidth = juce::jmax(150, noiseTrackWidth - reduce);
        }
        else
        {
            int extra = availableWidth - requiredWidth;
            area = area.withWidth(requiredWidth);
        }

        if (fileTrack && binauralTrack && noiseTrack)
        {
            auto fileArea = area.removeFromLeft(fileTrackWidth);
            fileTrack->setBounds(fileArea);
            auto fileMeterArea = area.removeFromLeft(meterWidth);
            fileMeterBounds = fileMeterArea;

            area.removeFromLeft(spacing);

            auto binauralArea = area.removeFromLeft(binauralTrackWidth);
            binauralTrack->setBounds(binauralArea);
            auto binauralMeterArea = area.removeFromLeft(meterWidth);
            binauralMeterBounds = binauralMeterArea;

            area.removeFromLeft(spacing);

            auto noiseArea = area.removeFromLeft(noiseTrackWidth);
            noiseTrack->setBounds(noiseArea);
            auto noiseMeterArea = area.removeFromLeft(meterWidth);
            noiseMeterBounds = noiseMeterArea;
        }
    }
    
    void setBackgroundColour(juce::Colour colour)
    {
        backgroundColour = colour;
        repaint();
    }
    
    // Methods to access track components
    BinauralTrackComponent* getBinauralTrack() { return binauralTrack.get(); }
    FileTrackComponent* getFileTrack() { return fileTrack.get(); }
    NoiseTrackComponent* getNoiseTrack() { return noiseTrack.get(); }
    
    // Methods to access audio sources
    NoiseAudioSource* getNoiseSource() { return noiseSource; }
    
    // Helper method to improve layout
    void getAudioTrackBounds() 
    {
        // This method is just here to trigger IDE error checking
        // It's called from MainComponent to improve readability
        return;
    }
    
    // Update track meters with audio levels
    void updateTrackMeters(float binauralLevel, float fileLevel, float noiseLevel)
    {
        if (binauralTrack) binauralTrack->setMeterLevel(binauralLevel);
        if (fileTrack) fileTrack->setMeterLevel(fileLevel);
        if (noiseTrack) noiseTrack->setMeterLevel(noiseLevel);
    }
    
    // Access meter bounds for external positioning
    juce::Rectangle<int> getBinauralMeterBounds() const { return binauralMeterBounds; }
    juce::Rectangle<int> getFileMeterBounds() const { return fileMeterBounds; }
    juce::Rectangle<int> getNoiseMeterBounds() const { return noiseMeterBounds; }

private:
    void handleSoloChanged(AudioTrackComponent* changedTrack)
    {
        // If this track is now soloed, un-solo the others
        if (changedTrack->isSolo())
        {
            if (changedTrack == binauralTrack.get()) {
                fileTrack->setSoloState(false);
                noiseTrack->setSoloState(false);
            }
            else if (changedTrack == fileTrack.get()) {
                binauralTrack->setSoloState(false);
                noiseTrack->setSoloState(false);
            }
            else if (changedTrack == noiseTrack.get()) {
                binauralTrack->setSoloState(false);
                fileTrack->setSoloState(false);
            }
        }
    }

    BinauralAudioSource* binauralSource = nullptr;
    FilePlayerAudioSource* filePlayer = nullptr;
    NoiseAudioSource* noiseSource = nullptr;

    std::unique_ptr<BinauralTrackComponent> binauralTrack;
    std::unique_ptr<FileTrackComponent>     fileTrack;
    std::unique_ptr<NoiseTrackComponent>    noiseTrack;
    
    juce::Colour backgroundColour;
    juce::Rectangle<int> separatorBounds;
    
    // Bounds for external meters positioned to the right of each track
    juce::Rectangle<int> binauralMeterBounds;
    juce::Rectangle<int> fileMeterBounds;
    juce::Rectangle<int> noiseMeterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPanel)
};
