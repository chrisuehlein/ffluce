#pragma once
#include <JuceHeader.h>

/**
 * NoiseAudioSource generates different types of noise (white, pink, brown)
 * with configurable gain and can be mixed with other audio sources.
 */
class NoiseAudioSource : public juce::AudioSource
{
public:
    enum NoiseType {
        White = 0,
        Pink = 1,
        Brown = 2
    };

    NoiseAudioSource();
    ~NoiseAudioSource() override;

    // AudioSource interface
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // Control methods
    void setGain(float newGain);
    float getGain() const { return gain; }
    
    void setNoiseType(NoiseType type);
    NoiseType getNoiseType() const { return noiseType; }
    
    void setMuted(bool shouldBeMuted);
    bool isMuted() const { return muted; }

private:
    // Noise generation
    void generateWhiteNoise(float* buffer, int numSamples);
    void generatePinkNoise(float* buffer, int numSamples);
    void generateBrownNoise(float* buffer, int numSamples);
    
    // Parameters
    float gain = 0.5f;
    NoiseType noiseType = White;
    bool muted = false;
    
    // Audio state
    double currentSampleRate = 44100.0;
    
    // Random number generator
    juce::Random random;
    
    // Pink noise filter state (Paul Kellet's algorithm)
    float pinkB0 = 0.0f, pinkB1 = 0.0f, pinkB2 = 0.0f, pinkB3 = 0.0f, pinkB4 = 0.0f, pinkB5 = 0.0f, pinkB6 = 0.0f;
    
    // Brown noise filter state
    float brownLastOutput = 0.0f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseAudioSource)
};