#include "NoiseAudioSource.h"

NoiseAudioSource::NoiseAudioSource()
{
    random.setSeedRandomly();
}

NoiseAudioSource::~NoiseAudioSource()
{
}

void NoiseAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
}

void NoiseAudioSource::releaseResources()
{
}

void NoiseAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (muted || gain <= 0.0f)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    auto* leftChannel = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* rightChannel = bufferToFill.buffer->getNumChannels() > 1 
                        ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample) 
                        : nullptr;

    // Generate noise into left channel
    switch (noiseType)
    {
        case White:
            generateWhiteNoise(leftChannel, bufferToFill.numSamples);
            break;
        case Pink:
            generatePinkNoise(leftChannel, bufferToFill.numSamples);
            break;
        case Brown:
            generateBrownNoise(leftChannel, bufferToFill.numSamples);
            break;
    }

    // Copy to right channel if stereo
    if (rightChannel != nullptr)
    {
        for (int i = 0; i < bufferToFill.numSamples; ++i)
            rightChannel[i] = leftChannel[i];
    }

    // Apply gain
    if (gain != 1.0f)
    {
        bufferToFill.buffer->applyGain(bufferToFill.startSample, bufferToFill.numSamples, gain);
    }
}

void NoiseAudioSource::setGain(float newGain)
{
    gain = juce::jlimit(0.0f, 2.0f, newGain);
}

void NoiseAudioSource::setNoiseType(NoiseType type)
{
    noiseType = type;
    
    // Reset filter states when changing noise type
    if (type == Pink)
    {
        pinkB0 = pinkB1 = pinkB2 = pinkB3 = pinkB4 = pinkB5 = pinkB6 = 0.0f;
    }
    else if (type == Brown)
    {
        brownLastOutput = 0.0f;
    }
}

void NoiseAudioSource::setMuted(bool shouldBeMuted)
{
    muted = shouldBeMuted;
}

void NoiseAudioSource::generateWhiteNoise(float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        buffer[i] = random.nextFloat() * 2.0f - 1.0f; // Range: -1.0 to 1.0
    }
}

void NoiseAudioSource::generatePinkNoise(float* buffer, int numSamples)
{
    // Paul Kellet's pink noise algorithm
    for (int i = 0; i < numSamples; ++i)
    {
        float white = random.nextFloat() * 2.0f - 1.0f;
        
        pinkB0 = 0.99886f * pinkB0 + white * 0.0555179f;
        pinkB1 = 0.99332f * pinkB1 + white * 0.0750759f;
        pinkB2 = 0.96900f * pinkB2 + white * 0.1538520f;
        pinkB3 = 0.86650f * pinkB3 + white * 0.3104856f;
        pinkB4 = 0.55000f * pinkB4 + white * 0.5329522f;
        pinkB5 = -0.7616f * pinkB5 - white * 0.0168980f;
        
        float pink = pinkB0 + pinkB1 + pinkB2 + pinkB3 + pinkB4 + pinkB5 + pinkB6 + white * 0.5362f;
        pinkB6 = white * 0.115926f;
        
        buffer[i] = pink * 0.11f; // Scale down to reasonable level
    }
}

void NoiseAudioSource::generateBrownNoise(float* buffer, int numSamples)
{
    // Brown noise (Brownian/random walk noise)
    for (int i = 0; i < numSamples; ++i)
    {
        float white = random.nextFloat() * 2.0f - 1.0f;
        brownLastOutput = (brownLastOutput + (0.02f * white)) / 1.02f;
        buffer[i] = brownLastOutput * 3.5f; // Scale up as brown noise is quieter
        
        // Clamp to prevent runaway
        brownLastOutput = juce::jlimit(-1.0f, 1.0f, brownLastOutput);
    }
}