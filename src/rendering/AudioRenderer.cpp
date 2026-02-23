#include "AudioRenderer.h"

AudioRenderer::AudioRenderer()
{
}

AudioRenderer::AudioRenderer(const std::vector<BinauralAudioSource*>& binauralSources,
                             const std::vector<FilePlayerAudioSource*>& filePlayers,
                             const std::vector<NoiseAudioSource*>& noiseSources)
    : binauralSources(binauralSources),
      filePlayers(filePlayers),
      noiseSources(noiseSources)
{
}

AudioRenderer::~AudioRenderer()
{
}

void AudioRenderer::setLogCallback(std::function<void(const juce::String&)> callback)
{
    logCallback = callback;
}

void AudioRenderer::setCancellationFlag(std::atomic<bool>* flag)
{
    cancelFlag = flag;
}

bool AudioRenderer::renderAudio(const juce::File& outputFile,
                                double durationSeconds,
                                double fadeInDuration,
                                double fadeOutDuration)
{
    if (logCallback)
        logCallback("Rendering audio track for " + juce::String(durationSeconds) + " seconds");

    if (logCallback)
    {
        logCallback("Audio source inventory:");
        logCallback("  Binaural tracks: " + juce::String((int)binauralSources.size()));
        logCallback("  File tracks: " + juce::String((int)filePlayers.size()));
        logCallback("  Noise tracks: " + juce::String((int)noiseSources.size()));
    }

    std::vector<std::unique_ptr<BinauralAudioSource>> renderBinauralSources;
    std::vector<std::unique_ptr<FilePlayerAudioSource>> renderFilePlayers;
    std::vector<std::unique_ptr<NoiseAudioSource>> renderNoiseSources;

    renderBinauralSources.reserve(binauralSources.size());
    renderFilePlayers.reserve(filePlayers.size());
    renderNoiseSources.reserve(noiseSources.size());

    for (auto* source : binauralSources)
    {
        if (source == nullptr)
            continue;

        auto copy = std::make_unique<BinauralAudioSource>();
        copy->setLeftFrequency(source->getLeftFrequency());
        copy->setRightFrequency(source->getRightFrequency());
        copy->setGain(source->getGain());
        renderBinauralSources.push_back(std::move(copy));
    }

    int fileTrackIndex = 0;
    for (auto* source : filePlayers)
    {
        ++fileTrackIndex;

        if (source == nullptr)
        {
            if (logCallback)
                logCallback("  File track " + juce::String(fileTrackIndex) + ": skipped (null source)");
            continue;
        }

        const float gain = source->getGain();
        if (gain <= 0.0001f)
        {
            if (logCallback)
                logCallback("  File track " + juce::String(fileTrackIndex) + ": skipped (muted/zero gain)");
            continue;
        }

        auto copy = std::make_unique<FilePlayerAudioSource>();
        auto playlist = source->getPlaylist();
        if (!playlist.empty())
        {
            const bool singleAudioItem = (playlist.size() == 1 &&
                                          playlist.front().type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile);
            const bool intendedInfiniteLoop = (singleAudioItem &&
                                               playlist.front().targetDurationSeconds <= 0.0 &&
                                               playlist.front().repetitions <= 0);
            if (intendedInfiniteLoop)
                playlist.front().repetitions = -1;

            copy->setPlaylist(playlist);
            copy->setGain(gain);
            copy->setPosition(0.0);
            copy->start();
            renderFilePlayers.push_back(std::move(copy));
            if (logCallback)
            {
                const auto& first = playlist.front();
                const juce::String firstType = first.type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile ? "audio" : "silence";
                logCallback("  File track " + juce::String(fileTrackIndex) + ": playlist mode (" +
                            juce::String((int)playlist.size()) + " items, gain=" + juce::String(gain, 4) +
                            ", firstFile=" + first.file.getFileName() +
                            ", firstType=" + firstType +
                            ", firstRepeats=" + juce::String(first.repetitions) +
                            ", firstTargetSec=" + juce::String(first.targetDurationSeconds, 3) +
                            ", firstCrossfadeSec=" + juce::String(first.crossfadeSeconds, 3) +
                            ", renderInfiniteLoop=" + juce::String(intendedInfiniteLoop ? "yes" : "no") + ")");
            }
            continue;
        }

        if (source->isLoaded())
        {
            const auto file = source->getLoadedFile();
            if (file.existsAsFile())
            {
                copy->loadFile(file);
                copy->setGain(gain);
                copy->setPosition(0.0);
                copy->start();
                renderFilePlayers.push_back(std::move(copy));
                if (logCallback)
                    logCallback("  File track " + juce::String(fileTrackIndex) + ": single file mode (" +
                                file.getFileName() + ", gain=" + juce::String(gain, 4) + ")");
            }
            else if (logCallback)
            {
                logCallback("  File track " + juce::String(fileTrackIndex) + ": skipped (missing file " +
                            file.getFullPathName() + ")");
            }
        }
        else if (logCallback)
        {
            logCallback("  File track " + juce::String(fileTrackIndex) + ": skipped (empty playlist)");
        }
    }

    for (auto* source : noiseSources)
    {
        if (source == nullptr)
            continue;

        auto copy = std::make_unique<NoiseAudioSource>();
        copy->setNoiseType(source->getNoiseType());
        copy->setGain(source->getGain());
        copy->setMuted(source->isMuted());
        renderNoiseSources.push_back(std::move(copy));
    }

    if (renderBinauralSources.empty() && renderFilePlayers.empty() && renderNoiseSources.empty())
    {
        if (logCallback)
            logCallback("ERROR: No audio source available");
        return false;
    }

    const int sampleRate = 44100;
    const int numChannels = 2;
    const juce::int64 totalSamples = static_cast<juce::int64>(durationSeconds * sampleRate);
    const int chunkSize = sampleRate * 10;
    const juce::int64 numChunks = (totalSamples + chunkSize - 1) / chunkSize;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    writer.reset(wavFormat.createWriterFor(new juce::FileOutputStream(outputFile),
                                           sampleRate,
                                           numChannels,
                                           24,
                                           {},
                                           0));

    if (writer == nullptr)
    {
        if (logCallback)
            logCallback("ERROR: Failed to create audio file writer");
        return false;
    }

    for (auto& source : renderBinauralSources)
        source->prepareToPlay(chunkSize, sampleRate);
    for (auto& source : renderFilePlayers)
        source->prepareToPlay(chunkSize, sampleRate);
    for (auto& source : renderNoiseSources)
        source->prepareToPlay(chunkSize, sampleRate);

    juce::AudioBuffer<float> tempBuffer;

    for (juce::int64 chunkIndex = 0; chunkIndex < numChunks; ++chunkIndex)
    {
        if (cancelFlag && cancelFlag->load())
        {
            if (logCallback)
                logCallback("Audio rendering cancelled");
            return false;
        }

        const juce::int64 startSample = chunkIndex * chunkSize;
        const int currentChunkSize = static_cast<int>(juce::jmin(static_cast<juce::int64>(chunkSize),
                                                                  totalSamples - startSample));
        if (currentChunkSize <= 0)
            break;

        if (logCallback && chunkIndex % 100 == 0)
            logCallback("  Processing chunk " + juce::String(chunkIndex + 1) + " of " + juce::String(numChunks));

        juce::AudioSampleBuffer chunkBuffer(numChannels, currentChunkSize);
        chunkBuffer.clear();

        auto mixSource = [&](juce::AudioSource* source)
        {
            if (source == nullptr)
                return;

            tempBuffer.setSize(numChannels, currentChunkSize, false, false, true);
            tempBuffer.clear();

            juce::AudioSourceChannelInfo info(&tempBuffer, 0, currentChunkSize);
            source->getNextAudioBlock(info);

            for (int channel = 0; channel < numChannels; ++channel)
                chunkBuffer.addFrom(channel, 0, tempBuffer, channel, 0, currentChunkSize);
        };

        for (auto& source : renderBinauralSources)
            mixSource(source.get());
        for (auto& source : renderFilePlayers)
            mixSource(source.get());
        for (auto& source : renderNoiseSources)
        {
            if (!source->isMuted())
                mixSource(source.get());
        }

        if (fadeInDuration > 0.0)
        {
            const int fadeInSamples = static_cast<int>(fadeInDuration * sampleRate);
            if (chunkIndex == 0)
            {
                const int samplesToFade = juce::jmin(fadeInSamples, currentChunkSize);
                applyFade(chunkBuffer, 0, samplesToFade, true);
            }
            else
            {
                const juce::int64 fadeEndSample = fadeInSamples;
                if (startSample < fadeEndSample)
                {
                    const int fadeStartInChunk = 0;
                    const int fadeEndInChunk = static_cast<int>(juce::jmin(fadeEndSample - startSample,
                                                                           static_cast<juce::int64>(currentChunkSize)));
                    const int samplesToFade = fadeEndInChunk - fadeStartInChunk;

                    if (samplesToFade > 0)
                    {
                        const float fadeStartPos = static_cast<float>(startSample) / fadeInSamples;
                        const float fadeEndPos = static_cast<float>(startSample + fadeEndInChunk) / fadeInSamples;
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
        }

        if (fadeOutDuration > 0.0)
        {
            const int fadeOutSamples = static_cast<int>(fadeOutDuration * sampleRate);
            const juce::int64 fadeOutStart = totalSamples - fadeOutSamples;
            if (startSample + currentChunkSize > fadeOutStart)
            {
                const juce::int64 fadeStartInChunk = juce::jmax(static_cast<juce::int64>(0), fadeOutStart - startSample);
                const int samplesToFade = currentChunkSize - static_cast<int>(fadeStartInChunk);
                if (samplesToFade > 0)
                {
                    const juce::int64 absoluteFadeStart = startSample + fadeStartInChunk;
                    const float fadeStartPos = static_cast<float>(absoluteFadeStart - fadeOutStart) / fadeOutSamples;
                    const float fadeEndPos = static_cast<float>(absoluteFadeStart + samplesToFade - fadeOutStart) / fadeOutSamples;
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

        float peakLevel = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            peakLevel = juce::jmax(peakLevel, chunkBuffer.getMagnitude(channel, 0, currentChunkSize));

        if (peakLevel > 0.891f)
        {
            const float limitFactor = 0.891f / peakLevel;
            for (int channel = 0; channel < numChannels; ++channel)
                chunkBuffer.applyGain(channel, 0, currentChunkSize, limitFactor);
        }

        if (!writer->writeFromAudioSampleBuffer(chunkBuffer, 0, currentChunkSize))
        {
            if (logCallback)
                logCallback("ERROR: Failed to write audio chunk " + juce::String(chunkIndex));
            return false;
        }
    }

    writer.reset();

    if (outputFile.existsAsFile())
    {
        const juce::int64 fileSize = outputFile.getSize();
        if (logCallback)
        {
            logCallback("  Audio file created successfully: " + outputFile.getFullPathName());
            logCallback("  File size: " + juce::String(fileSize / 1024 / 1024) + " MB");
            logCallback("  Expected size: ~" + juce::String((totalSamples * numChannels * 3) / 1024 / 1024) + " MB (24-bit)");
        }
        return true;
    }

    if (logCallback)
        logCallback("ERROR: Audio file was not created");
    return false;
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

            const float gain = alpha * alpha * (3.0f - 2.0f * alpha);
            data[i] *= gain;
        }
    }
}

bool AudioRenderer::renderFilePlayerOutput(FilePlayerAudioSource* filePlayer,
                                           double durationSeconds,
                                           const juce::File& outputFile)
{
    filePlayers.clear();
    if (filePlayer != nullptr)
        filePlayers.push_back(filePlayer);

    return renderFilePlayerOutput(outputFile, durationSeconds, 1.0, 1.0);
}

bool AudioRenderer::renderFilePlayerOutput(const juce::File& outputFile,
                                           double durationSeconds,
                                           double fadeInDuration,
                                           double fadeOutDuration)
{
    if (filePlayers.empty())
    {
        if (logCallback)
            logCallback("ERROR: No file player audio source available");
        return false;
    }

    auto savedBinaural = binauralSources;
    auto savedNoise = noiseSources;
    binauralSources.clear();
    noiseSources.clear();

    const bool success = renderAudio(outputFile, durationSeconds, fadeInDuration, fadeOutDuration);

    binauralSources = savedBinaural;
    noiseSources = savedNoise;
    return success;
}

bool AudioRenderer::renderBinauralOutput(BinauralAudioSource* binauralSource,
                                         double durationSeconds,
                                         const juce::File& outputFile)
{
    binauralSources.clear();
    if (binauralSource != nullptr)
        binauralSources.push_back(binauralSource);

    auto savedFilePlayers = filePlayers;
    auto savedNoise = noiseSources;
    filePlayers.clear();
    noiseSources.clear();

    const bool success = renderAudio(outputFile, durationSeconds, 1.0, 1.0);

    filePlayers = savedFilePlayers;
    noiseSources = savedNoise;
    return success;
}
