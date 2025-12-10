#pragma once
#include <JuceHeader.h>
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "RenderTypes.h"

/**
 * Handles the rendering of audio from binaural, file, and noise sources.
 */
class AudioRenderer
{
public:
    /**
     * Default constructor
     */
    AudioRenderer();
    
    /**
     * Creates a new AudioRenderer.
     * @param binauralSource The binaural audio source to render from
     * @param filePlayer The file audio source to render from (optional)
     * @param noiseSource The noise audio source to render from (optional)
     */
    AudioRenderer(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer = nullptr, NoiseAudioSource* noiseSource = nullptr);
    ~AudioRenderer();
    
    /**
     * Sets a callback for receiving log messages.
     * @param logCallback Function called with log messages
     */
    void setLogCallback(std::function<void(const juce::String&)> logCallback);
    
    /**
     * Renders audio to a file.
     * @param outputFile The file to save the audio to
     * @param durationSeconds The duration of the audio in seconds
     * @param fadeInDuration The duration of the fade-in in seconds
     * @param fadeOutDuration The duration of the fade-out in seconds
     * @return true if the operation was successful, false otherwise
     */
    bool renderAudio(const juce::File& outputFile,
                    double durationSeconds,
                    double fadeInDuration,
                    double fadeOutDuration);
                    
    /**
     * Renders audio from the file player to a file.
     * @param outputFile The file to save the audio to
     * @param durationSeconds The duration of the audio in seconds
     * @param fadeInDuration The duration of the fade-in in seconds
     * @param fadeOutDuration The duration of the fade-out in seconds
     * @return true if the operation was successful, false otherwise
     */
    bool renderFilePlayerOutput(const juce::File& outputFile,
                                double durationSeconds,
                                double fadeInDuration,
                                double fadeOutDuration);
                                
    /**
     * Simplified method to render output from the file player.
     * @param filePlayer The file player audio source 
     * @param durationSeconds The duration of the audio in seconds
     * @param outputFile The file to save the audio to
     * @return true if the operation was successful, false otherwise
     */
    bool renderFilePlayerOutput(FilePlayerAudioSource* filePlayer,
                                double durationSeconds,
                                const juce::File& outputFile);
                                
    /**
     * Simplified method to render output from the binaural source.
     * @param binauralSource The binaural audio source
     * @param durationSeconds The duration of the audio in seconds
     * @param outputFile The file to save the audio to
     * @return true if the operation was successful, false otherwise
     */
    bool renderBinauralOutput(BinauralAudioSource* binauralSource,
                             double durationSeconds,
                             const juce::File& outputFile);
    
private:
    /**
     * Applies a fade to an audio buffer.
     * @param buffer The audio buffer to apply the fade to
     * @param startSample The sample index to start the fade at
     * @param numSamples The number of samples to fade
     * @param fadeIn true for fade in, false for fade out
     */
    void applyFade(juce::AudioSampleBuffer& buffer,
                  int startSample,
                  int numSamples,
                  bool fadeIn);
    
    // Audio sources
    BinauralAudioSource* binauralSource;
    FilePlayerAudioSource* filePlayer;
    NoiseAudioSource* noiseSource;
    
    // Log callback
    std::function<void(const juce::String&)> logCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRenderer)
};