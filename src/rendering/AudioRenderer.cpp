#include "AudioRenderer.h"

AudioRenderer::AudioRenderer()
    : binauralSource(nullptr),
      filePlayer(nullptr),
      noiseSource(nullptr)
{
}

AudioRenderer::AudioRenderer(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer, NoiseAudioSource* noiseSource)
    : binauralSource(binauralSource),
      filePlayer(filePlayer),
      noiseSource(noiseSource)
{
}

AudioRenderer::~AudioRenderer()
{
}

void AudioRenderer::setLogCallback(std::function<void(const juce::String&)> callback)
{
    logCallback = callback;
}

bool AudioRenderer::renderAudio(const juce::File& outputFile,
                             double durationSeconds,
                             double fadeInDuration,
                             double fadeOutDuration)
{
    if (logCallback)
        logCallback("Rendering audio track for " + juce::String(durationSeconds) + " seconds");
    
    if (!binauralSource && !filePlayer && !noiseSource)
    {
        if (logCallback)
            logCallback("ERROR: No audio source available");
        return false;
    }

    // Create separate copies of the audio sources for rendering
    std::unique_ptr<FilePlayerAudioSource> renderFilePlayer;
    std::unique_ptr<BinauralAudioSource> renderBinauralSource;
    std::unique_ptr<NoiseAudioSource> renderNoiseSource;
    
    if (filePlayer != nullptr)
    {
        const auto playlist = filePlayer->getPlaylist();
        if (!playlist.empty())
        {
            renderFilePlayer = std::make_unique<FilePlayerAudioSource>();
            renderFilePlayer->setPlaylist(playlist);
            renderFilePlayer->setGain(filePlayer->getGain());
            renderFilePlayer->start();
        }
        else if (filePlayer->isLoaded())
        {
            renderFilePlayer = std::make_unique<FilePlayerAudioSource>();
            juce::File originalFile = filePlayer->getLoadedFile();
            if (originalFile.existsAsFile())
            {
                renderFilePlayer->loadFile(originalFile);
                renderFilePlayer->setGain(filePlayer->getGain());
                renderFilePlayer->start();
            }
        }
    }
    
    if (binauralSource != nullptr)
    {
        renderBinauralSource = std::make_unique<BinauralAudioSource>();
        renderBinauralSource->setLeftFrequency(binauralSource->getLeftFrequency());
        renderBinauralSource->setRightFrequency(binauralSource->getRightFrequency());
        renderBinauralSource->setGain(binauralSource->getGain());
    }

    if (noiseSource != nullptr)
    {
        renderNoiseSource = std::make_unique<NoiseAudioSource>();
        renderNoiseSource->setNoiseType(noiseSource->getNoiseType());
        renderNoiseSource->setGain(noiseSource->getGain());
        renderNoiseSource->setMuted(noiseSource->isMuted());
    }

    FilePlayerAudioSource* sourcePlayer = renderFilePlayer ? renderFilePlayer.get() : filePlayer;
    BinauralAudioSource* sourceBinaural = renderBinauralSource ? renderBinauralSource.get() : binauralSource;
    NoiseAudioSource* sourceNoise = renderNoiseSource ? renderNoiseSource.get() : noiseSource;

    const int sampleRate = 44100;
    const int numChannels = 2;
    const juce::int64 totalSamples = static_cast<juce::int64>(durationSeconds * sampleRate);

    // Use chunked rendering for large files to avoid memory allocation failures
    const int chunkSize = sampleRate * 10;
    const juce::int64 numChunks = (totalSamples + chunkSize - 1) / chunkSize;
    
    // Create WAV file writer first
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    
    writer.reset(wavFormat.createWriterFor(new juce::FileOutputStream(outputFile),
                                           sampleRate,
                                           numChannels,
                                           24,   // 24-bit depth for high-quality audio
                                           {},   // No metadata
                                           0));  // No compression
    
    if (writer == nullptr)
    {
        try {
            if (logCallback)
                logCallback("ERROR: Failed to create audio file writer");
        }
        catch (const std::exception& e) {
            // Continue even if logging fails
        }
        return false;
    }
    
    // Initialize audio sources once
    if (sourceBinaural)
        sourceBinaural->prepareToPlay(chunkSize, sampleRate);
    if (sourcePlayer)
        sourcePlayer->prepareToPlay(chunkSize, sampleRate);
    if (sourceNoise && !sourceNoise->isMuted())
        sourceNoise->prepareToPlay(chunkSize, sampleRate);
    
    // Process audio in chunks
    for (juce::int64 chunkIndex = 0; chunkIndex < numChunks; ++chunkIndex)
    {
        const juce::int64 startSample = chunkIndex * chunkSize;
        const int currentChunkSize = static_cast<int>(juce::jmin(static_cast<juce::int64>(chunkSize), 
                                                                  totalSamples - startSample));
        
        if (logCallback && chunkIndex % 100 == 0) {
            logCallback("  Processing chunk " + juce::String(chunkIndex + 1) + " of " + juce::String(numChunks));
        }
        
        // Create buffer for this chunk
        juce::AudioSampleBuffer chunkBuffer(numChannels, currentChunkSize);
        chunkBuffer.clear();
        
        // Fill buffer with audio from sources
        juce::AudioSourceChannelInfo info(&chunkBuffer, 0, currentChunkSize);
    
        // Render binaural audio if available
        if (sourceBinaural)
        {
            try {
                // Get the binaural audio - use our render copy
                sourceBinaural->getNextAudioBlock(info);
            } catch (const std::exception& e) {
                if (logCallback) {
                    logCallback("  ERROR getting audio from binaural source: " + juce::String(e.what()));
                }
            }
        }
    
        // Mix in file player audio if available
        if (sourcePlayer)
        {
            // Create temporary buffer for file audio
            juce::AudioSampleBuffer fileBuffer(numChannels, currentChunkSize);
            fileBuffer.clear();
            
            juce::AudioSourceChannelInfo fileInfo(&fileBuffer, 0, currentChunkSize);
            
            // Get audio data from file player
            try {
                // Get the file audio - use our render copy that's already in "playing" mode
                sourcePlayer->getNextAudioBlock(fileInfo);
            } catch (const std::exception& e) {
                if (logCallback) {
                    logCallback("  ERROR getting audio from file player: " + juce::String(e.what()));
                }
            }
            
            // Mix into main buffer
            for (int channel = 0; channel < numChannels; ++channel)
            {
                chunkBuffer.addFrom(channel, 0, fileBuffer, channel, 0, currentChunkSize);
            }
        }
    
        // Mix in noise audio if available and not muted
        if (sourceNoise && !sourceNoise->isMuted())
        {
            // Create temporary buffer for noise audio
            juce::AudioSampleBuffer noiseBuffer(numChannels, currentChunkSize);
            noiseBuffer.clear();
            
            juce::AudioSourceChannelInfo noiseInfo(&noiseBuffer, 0, currentChunkSize);
            
            // Get audio data from noise source
            try {
                // Get the noise audio
                sourceNoise->getNextAudioBlock(noiseInfo);
            } catch (const std::exception& e) {
                if (logCallback) {
                    logCallback("  ERROR getting audio from noise source: " + juce::String(e.what()));
                }
            }
            
            // Mix into main buffer
            for (int channel = 0; channel < numChannels; ++channel)
            {
                chunkBuffer.addFrom(channel, 0, noiseBuffer, channel, 0, currentChunkSize);
            }
        }
        
        // Apply fade-in to first chunk if needed
        if (chunkIndex == 0 && fadeInDuration > 0.0)
        {
            const int fadeInSamples = static_cast<int>(fadeInDuration * sampleRate);
            const int samplesToFade = juce::jmin(fadeInSamples, currentChunkSize);
            applyFade(chunkBuffer, 0, samplesToFade, true);
        }
        else if (fadeInDuration > 0.0)
        {
            // Continue fade-in if it extends beyond first chunk
            const int fadeInSamples = static_cast<int>(fadeInDuration * sampleRate);
            const juce::int64 fadeEndSample = fadeInSamples;
            if (startSample < fadeEndSample)
            {
                const int fadeStartInChunk = 0;
                const int fadeEndInChunk = static_cast<int>(juce::jmin(fadeEndSample - startSample, 
                                                                       static_cast<juce::int64>(currentChunkSize)));
                const int samplesToFade = fadeEndInChunk - fadeStartInChunk;
                
                if (samplesToFade > 0)
                {
                    // Calculate fade position relative to overall fade
                    const float fadeStartPos = static_cast<float>(startSample) / fadeInSamples;
                    const float fadeEndPos = static_cast<float>(startSample + fadeEndInChunk) / fadeInSamples;
                    
                    // Apply partial fade
                    for (int channel = 0; channel < numChannels; ++channel)
                    {
                        float* channelData = chunkBuffer.getWritePointer(channel);
                        for (int i = 0; i < samplesToFade; ++i)
                        {
                            const float fadePosition = fadeStartPos + (fadeEndPos - fadeStartPos) * i / samplesToFade;
                            channelData[i] *= fadePosition;
                        }
                    }
                }
            }
        }
        
        // Apply fade-out to last chunks if needed
        if (fadeOutDuration > 0.0)
        {
            const int fadeOutSamples = static_cast<int>(fadeOutDuration * sampleRate);
            const juce::int64 fadeOutStart = totalSamples - fadeOutSamples;
            
            if (startSample + currentChunkSize > fadeOutStart)
            {
                const juce::int64 fadeStartInChunk = juce::jmax(static_cast<juce::int64>(0), fadeOutStart - startSample);
                const int fadeEndInChunk = currentChunkSize;
                const int samplesToFade = fadeEndInChunk - static_cast<int>(fadeStartInChunk);
                
                if (samplesToFade > 0)
                {
                    // Calculate fade position relative to overall fade
                    const juce::int64 absoluteFadeStart = startSample + fadeStartInChunk;
                    const float fadeStartPos = static_cast<float>(absoluteFadeStart - fadeOutStart) / fadeOutSamples;
                    const float fadeEndPos = static_cast<float>(absoluteFadeStart + samplesToFade - fadeOutStart) / fadeOutSamples;
                    
                    // Apply partial fade
                    for (int channel = 0; channel < numChannels; ++channel)
                    {
                        float* channelData = chunkBuffer.getWritePointer(channel);
                        for (int i = 0; i < samplesToFade; ++i)
                        {
                            const float fadePosition = fadeStartPos + (fadeEndPos - fadeStartPos) * i / samplesToFade;
                            channelData[static_cast<int>(fadeStartInChunk) + i] *= (1.0f - fadePosition);
                        }
                    }
                }
            }
        }
        
        // Apply limiter to this chunk if needed
        float peakLevel = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            peakLevel = juce::jmax(peakLevel, chunkBuffer.getMagnitude(channel, 0, currentChunkSize));
        }
        
        if (peakLevel > 0.891f) // -1dB threshold
        {
            const float limitFactor = 0.891f / peakLevel;
            for (int channel = 0; channel < numChannels; ++channel)
            {
                chunkBuffer.applyGain(channel, 0, currentChunkSize, limitFactor);
            }
        }
        
        // Write this chunk to file
        bool writeSuccess = writer->writeFromAudioSampleBuffer(chunkBuffer, 0, currentChunkSize);
        if (!writeSuccess)
        {
            if (logCallback)
                logCallback("ERROR: Failed to write audio chunk " + juce::String(chunkIndex));
            return false;
        }
    }
    
    // Close the writer
    writer.reset();
    
    // Verify file was created
    if (outputFile.existsAsFile())
    {
        juce::int64 fileSize = outputFile.getSize();
        if (logCallback)
        {
            logCallback("  Audio file created successfully: " + outputFile.getFullPathName());
            logCallback("  File size: " + juce::String(fileSize / 1024 / 1024) + " MB");
            logCallback("  Expected size: ~" + juce::String((totalSamples * numChannels * 3) / 1024 / 1024) + " MB (24-bit)");
        }
        return true;
    }
    else
    {
        if (logCallback)
            logCallback("ERROR: Audio file was not created");
        return false;
    }
}

void AudioRenderer::applyFade(juce::AudioSampleBuffer& buffer,
                           int startSample,
                           int numSamples,
                           bool fadeIn)
{
    const int numChannels = buffer.getNumChannels();
    
    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* data = buffer.getWritePointer(channel, startSample);
        
        for (int i = 0; i < numSamples; ++i)
        {
            float alpha = static_cast<float>(i) / static_cast<float>(numSamples);
            
            if (!fadeIn)
                alpha = 1.0f - alpha;
                
            // Apply cubic fade curve for smoother sound
            float gain = alpha * alpha * (3.0f - 2.0f * alpha);
            
            data[i] *= gain;
        }
    }
}

bool AudioRenderer::renderFilePlayerOutput(FilePlayerAudioSource* filePlayer,
                                         double durationSeconds,
                                         const juce::File& outputFile)
{
    if (logCallback)
        logCallback("Rendering file player audio track for " + juce::String(durationSeconds) + " seconds");

    if (!filePlayer)
    {
        if (logCallback)
            logCallback("ERROR: No file player audio source available");
        return false;
    }
    
    // Apply default fade values
    // Store the passed filePlayer in the class member
    this->filePlayer = filePlayer;
    return renderFilePlayerOutput(outputFile, durationSeconds, 1.0, 1.0);
}

bool AudioRenderer::renderFilePlayerOutput(const juce::File& outputFile,
                                         double durationSeconds,
                                         double fadeInDuration,
                                         double fadeOutDuration)
{
    try {
        if (logCallback)
            logCallback("Rendering file player audio track for " + juce::String(durationSeconds) + " seconds");
    }
    catch (const std::exception& e) {
        // Continue even if logging fails
    }
    
    // Check if we have a valid file player
    if (!filePlayer)
    {
        try {
            if (logCallback)
                logCallback("ERROR: No file player audio source available");
        }
        catch (const std::exception& e) {
            // Continue even if logging fails
        }
        return false;
    }
    
    // Sample rate and channels
    const int sampleRate = 44100;
    const int numChannels = 2;
    
    // Calculate total samples
    const int totalSamples = static_cast<int>(durationSeconds * sampleRate);
    
    // Create audio buffer
    juce::AudioSampleBuffer buffer(numChannels, totalSamples);
    buffer.clear();
    
    // Fill buffer with audio from file player
    juce::AudioSourceChannelInfo info(&buffer, 0, totalSamples);
    
    try {
        if (logCallback)
            logCallback("  Rendering file audio");
    }
    catch (const std::exception& e) {
        // Continue even if logging fails
    }
    
    filePlayer->prepareToPlay(buffer.getNumSamples(), sampleRate);
    filePlayer->getNextAudioBlock(info);
    
    // Apply fade-in
    if (fadeInDuration > 0.0)
    {
        const int fadeInSamples = static_cast<int>(fadeInDuration * sampleRate);
        applyFade(buffer, 0, fadeInSamples, true);
    }
    
    // Apply fade-out
    if (fadeOutDuration > 0.0)
    {
        const int fadeOutSamples = static_cast<int>(fadeOutDuration * sampleRate);
        const int fadeOutStart = totalSamples - fadeOutSamples;
        applyFade(buffer, fadeOutStart, fadeOutSamples, false);
    }
    
    // Apply limiter at -1dB if needed
    float peakLevel = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
    {
        peakLevel = juce::jmax(peakLevel, buffer.getMagnitude(channel, 0, totalSamples));
    }
    
    if (peakLevel > 0.0f)
    {
        // Only apply limiting if the peak level exceeds our -1dB target (approx 0.891)
        if (peakLevel > 0.891f)
        {
            // Apply limiting to -1dB to prevent clipping
            const float limitFactor = 0.891f / peakLevel;
            
            if (logCallback)
                logCallback("  Limiting audio peak to -1dB with factor: " + juce::String(limitFactor) + 
                           " (peak was " + juce::String(peakLevel) + ")");
            
            for (int channel = 0; channel < numChannels; ++channel)
            {
                buffer.applyGain(channel, 0, totalSamples, limitFactor);
            }
        }
        else
        {
            if (logCallback)
                logCallback("  Audio level is good (peak: " + juce::String(peakLevel) + 
                           "), no limiting needed");
        }
    }
    
    // Create WAV file
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    
    writer.reset(wavFormat.createWriterFor(new juce::FileOutputStream(outputFile),
                                         sampleRate,
                                         numChannels,
                                         24,   // 24-bit depth
                                         {},   // No metadata
                                         0));  // No compression
    
    if (writer == nullptr)
    {
        try {
            if (logCallback)
                logCallback("ERROR: Failed to create audio file writer");
        }
        catch (const std::exception& e) {
            // Continue even if logging fails
        }
        return false;
    }
    
    // Write the buffer to file
    writer->writeFromAudioSampleBuffer(buffer, 0, totalSamples);
    
    try {
        if (logCallback)
            logCallback("  File audio rendering complete: " + outputFile.getFullPathName());
    }
    catch (const std::exception& e) {
        // Continue even if logging fails
    }
    
    return true;
}

bool AudioRenderer::renderBinauralOutput(BinauralAudioSource* binauralSource,
                                       double durationSeconds,
                                       const juce::File& outputFile)
{
    if (logCallback)
        logCallback("Rendering binaural audio track for " + juce::String(durationSeconds) + " seconds");

    if (!binauralSource)
    {
        if (logCallback)
            logCallback("ERROR: No binaural audio source available");
        return false;
    }

    this->binauralSource = binauralSource;
    return renderAudio(outputFile, durationSeconds, 1.0, 1.0);
}
