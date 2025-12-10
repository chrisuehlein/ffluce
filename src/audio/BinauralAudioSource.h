/*
  ==============================================================================

    BinauralAudioSource.h
    Created: 18 Feb 2025 7:23:48pm
    Author:  chris

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

/**
    BinauralAudioSource:
      - Two sine waves (leftFrequency, rightFrequency)
      - setGain() for controlling track volume
*/
class BinauralAudioSource : public juce::AudioSource
{
public:
    BinauralAudioSource() {}

    void prepareToPlay (int, double sampleRate) override
    {
        currentSampleRate = sampleRate;
        leftPhase = 0.0;
        rightPhase = 0.0;
    }
    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        auto* left  = bufferToFill.buffer->getWritePointer (0, bufferToFill.startSample);
        auto* right = bufferToFill.buffer->getWritePointer (1, bufferToFill.startSample);
        auto numSamples = bufferToFill.numSamples;

        double incLeft  = juce::MathConstants<double>::twoPi * leftFrequency  / currentSampleRate;
        double incRight = juce::MathConstants<double>::twoPi * rightFrequency / currentSampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = (float) (std::sin (leftPhase)  * currentGain);
            right[i] = (float) (std::sin (rightPhase) * currentGain);

            leftPhase  += incLeft;
            rightPhase += incRight;
        }
    }

    void setLeftFrequency  (double freq) { leftFrequency  = freq; }
    void setRightFrequency (double freq) { rightFrequency = freq; }
    double getLeftFrequency() const      { return leftFrequency; }
    double getRightFrequency() const     { return rightFrequency; }
    void setGain (float g)               { currentGain    = g;   }
    float getGain() const                { return currentGain;   }
    
    // Helper method to check if the source is actually playing (useful for UI controls)
    bool isPlaying() const { return currentGain > 0.0f; }

private:
    double currentSampleRate = 44100.0;
    double leftPhase = 0.0, rightPhase = 0.0;
    double leftFrequency = 70.0, rightFrequency = 74.0;
    float  currentGain = 0.0f;
};
