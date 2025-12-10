#pragma once

#include <JuceHeader.h>
#include <vector>

/**
    FilePlayerAudioSource
    ---------------------
    - Plays back either a single looping file (legacy behaviour) or a user-defined playlist.
    - Playlist entries can be audio files or silence gaps, each with repeat/duration targets and crossfades.
    - Wraps AudioFormatReaderSource + ResamplingAudioSource for automatic sample-rate matching.
*/
class FilePlayerAudioSource : public juce::AudioSource
{
public:
    struct PlaylistItem
    {
        enum class ItemType { AudioFile, Silence };

        ItemType type{ ItemType::AudioFile };
        juce::File file;
        juce::String displayName;
        double targetDurationSeconds{ 0.0 };   // If > 0, overrides repetitions
        int repetitions{ 1 };                  // <=0 means infinite (single file mode)
        double crossfadeSeconds{ 1.0 };

        static PlaylistItem createAudio(const juce::File& f,
                                        int repetitions,
                                        double targetDuration,
                                        double crossfade,
                                        juce::String name = {})
        {
            PlaylistItem item;
            item.type = ItemType::AudioFile;
            item.file = f;
            item.displayName = name.isNotEmpty() ? name : f.getFileName();
            item.repetitions = repetitions;
            item.targetDurationSeconds = targetDuration;
            item.crossfadeSeconds = juce::jmax(0.0, crossfade);
            return item;
        }

        static PlaylistItem createSilence(double durationSeconds,
                                          double crossfade,
                                          juce::String label = "Silence")
        {
            PlaylistItem item;
            item.type = ItemType::Silence;
            item.displayName = label;
            item.targetDurationSeconds = juce::jmax(0.0, durationSeconds);
            item.crossfadeSeconds = juce::jmax(0.0, crossfade);
            return item;
        }
    };

    FilePlayerAudioSource()
    {
        formatManager.registerBasicFormats();
    }

    ~FilePlayerAudioSource() override
    {
        resetPlaybackChain();
        playlistReader.reset();
        playlistResampler.reset();
    }

    //==============================================================================
    void loadFile(const juce::File& audioFile)
    {
        // Legacy single-file mode: treat as a playlist with one infinitely looping entry
        PlaylistItem single = PlaylistItem::createAudio(audioFile, -1, 0.0, 0.0);
        setPlaylist({ single });
        loadedFile = audioFile;
        loadedFileSampleRate = 0.0;
    }

    void start()  { isPlaying = true; }
    void stop()   { isPlaying = false; }

    bool isLoaded() const { return playlistMode ? !playlistItems.empty() : (playlistReader != nullptr); }

    void setGain(float g)  { currentGain = g; }
    float getGain() const  { return currentGain; }

    double getFileSampleRate() const { return loadedFileSampleRate; }
    juce::File getLoadedFile() const { return loadedFile; }

    const std::vector<PlaylistItem>& getPlaylist() const noexcept { return playlistItems; }

    void setPosition(double positionInSeconds)
    {
        if (playlistMode)
        {
            juce::ignoreUnused(positionInSeconds);
            resetPlaylistState();
        }
        else if (playlistReader != nullptr && loadedFileSampleRate > 0.0)
        {
            auto positionInSamples = static_cast<juce::int64>(positionInSeconds * loadedFileSampleRate);
            playlistReader->setNextReadPosition(positionInSamples);
        }
    }

    void setPlaylist(const std::vector<PlaylistItem>& items)
    {
        playlistItems = items;
        playlistMode = !playlistItems.empty();
        resetPlaybackChain();
        resetPlaylistState();
    }

    void clearPlaylist()
    {
        playlistItems.clear();
        playlistMode = false;
        resetPlaybackChain();
        resetPlaylistState();
    }

    //==============================================================================
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        deviceSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
        lastBlockSize = samplesPerBlockExpected > 0 ? samplesPerBlockExpected : 512;

        if (playlistMode)
        {
            if (playlistResampler)
                playlistResampler->prepareToPlay(samplesPerBlockExpected, deviceSampleRate);
        }
        else if (playlistResampler)
        {
            playlistResampler->prepareToPlay(samplesPerBlockExpected, deviceSampleRate);
        }
    }

    void releaseResources() override
    {
        if (playlistResampler)
            playlistResampler->releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        if (!isPlaying)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        if (playlistMode && !playlistItems.empty())
        {
            renderPlaylistBlock(bufferToFill);
        }
        else
        {
            renderSingleFileBlock(bufferToFill);
        }

        if (std::abs(currentGain - 1.0f) > 0.0001f && bufferToFill.buffer != nullptr)
        {
            bufferToFill.buffer->applyGain(bufferToFill.startSample, bufferToFill.numSamples, currentGain);
        }
    }

private:
    struct ActivePlaylistState
    {
        int itemIndex{-1};
        double samplesRemaining{0.0};
        double itemTotalSamples{0.0};
        double crossfadeSamples{0.0};
        bool infinite{false};

        void reset()
        {
            itemIndex = -1;
            samplesRemaining = 0.0;
            itemTotalSamples = 0.0;
            crossfadeSamples = 0.0;
            infinite = false;
        }
    };

    void resetPlaybackChain()
    {
        playlistReader.reset();
        playlistResampler.reset();
        clearUpcomingState();
    }

    void resetPlaylistState()
    {
        activeState.reset();
        playlistIndex = -1;
        clearUpcomingState();
    }

    bool ensureActivePlaylistItem()
    {
        if (activeState.samplesRemaining > 0.0 || activeState.infinite)
            return true;

        return advanceToNextPlaylistItem();
    }

    bool advanceToNextPlaylistItem()
    {
        if (playlistItems.empty())
            return false;

        clearUpcomingState();
        playlistIndex = (playlistIndex + 1) % (int)playlistItems.size();
        return preparePlaylistStateForIndex(playlistIndex, activeState, playlistReader, playlistResampler);
    }

    bool loadPlaylistReader(const PlaylistItem& item,
                            std::unique_ptr<juce::AudioFormatReaderSource>& readerTarget,
                            std::unique_ptr<juce::ResamplingAudioSource>& resamplerTarget)
    {
        readerTarget.reset();
        resamplerTarget.reset();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(item.file));
        if (reader == nullptr)
            return false;

        loadedFileSampleRate = reader->sampleRate;
        loadedFile = item.file;

        auto* rawReader = new juce::AudioFormatReaderSource(reader.release(), true);
        rawReader->setLooping(true);
        readerTarget.reset(rawReader);

        auto* rawResampler = new juce::ResamplingAudioSource(readerTarget.get(), false, 2);
        resamplerTarget.reset(rawResampler);
        resamplerTarget->prepareToPlay(lastBlockSize, deviceSampleRate);
        return true;
    }

    bool preparePlaylistStateForIndex(int index,
                                      ActivePlaylistState& state,
                                      std::unique_ptr<juce::AudioFormatReaderSource>& readerTarget,
                                      std::unique_ptr<juce::ResamplingAudioSource>& resamplerTarget)
    {
        if (playlistItems.empty() || index < 0 || index >= (int)playlistItems.size())
            return false;

        const auto& item = playlistItems[index];
        state.itemIndex = index;
        state.crossfadeSamples = juce::jmax(0.0, item.crossfadeSeconds * deviceSampleRate);
        state.infinite = (item.type == PlaylistItem::ItemType::AudioFile &&
                          item.repetitions <= 0 && item.targetDurationSeconds <= 0.0);
        state.itemTotalSamples = computeItemLengthSamples(item);
        state.samplesRemaining = state.itemTotalSamples;

        if (item.type == PlaylistItem::ItemType::AudioFile)
        {
            if (!loadPlaylistReader(item, readerTarget, resamplerTarget))
            {
                state.samplesRemaining = 0.0;
                return false;
            }
        }
        else
        {
            readerTarget.reset();
            resamplerTarget.reset();
        }

        return true;
    }

    bool ensureUpcomingItemPrepared()
    {
        if (upcomingStateValid)
            return true;

        if (playlistItems.size() <= 1)
            return false;

        const int nextIndex = (playlistIndex + 1) % (int)playlistItems.size();
        if (!preparePlaylistStateForIndex(nextIndex, upcomingState, upcomingReader, upcomingResampler))
            return false;

        upcomingStateValid = true;
        return true;
    }

    void promoteUpcomingItem()
    {
        if (!upcomingStateValid)
        {
            advanceToNextPlaylistItem();
            return;
        }

        playlistReader = std::move(upcomingReader);
        playlistResampler = std::move(upcomingResampler);
        activeState = upcomingState;
        playlistIndex = activeState.itemIndex;
        upcomingState.reset();
        upcomingStateValid = false;
        crossfadeInProgress = false;
    }

    void clearUpcomingState()
    {
        upcomingReader.reset();
        upcomingResampler.reset();
        upcomingState.reset();
        upcomingStateValid = false;
        crossfadeInProgress = false;
    }

    double computeItemLengthSamples(const PlaylistItem& item)
    {
        if (item.type == PlaylistItem::ItemType::Silence)
            return juce::jmax(0.0, item.targetDurationSeconds) * deviceSampleRate;

        double lengthSeconds = 0.0;
        if (item.targetDurationSeconds > 0.0)
        {
            lengthSeconds = item.targetDurationSeconds;
        }
        else
        {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(item.file));
            if (reader != nullptr)
            {
                double fileSeconds = reader->lengthInSamples / reader->sampleRate;
                if (item.repetitions <= 0)
                    lengthSeconds = fileSeconds;
                else
                    lengthSeconds = fileSeconds * item.repetitions;
            }
        }

        return juce::jmax(0.0, lengthSeconds) * deviceSampleRate;
    }

    void renderSingleFileBlock(const juce::AudioSourceChannelInfo& bufferToFill)
    {
        if (playlistReader == nullptr || playlistResampler == nullptr)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        juce::AudioSourceChannelInfo info(bufferToFill.buffer,
                                          bufferToFill.startSample,
                                          bufferToFill.numSamples);
        playlistResampler->getNextAudioBlock(info);
    }

    void renderPlaylistBlock(const juce::AudioSourceChannelInfo& bufferToFill)
    {
        if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
            return;

        int samplesRemaining = bufferToFill.numSamples;
        int destPos = bufferToFill.startSample;
        auto* outBuffer = bufferToFill.buffer;

        while (samplesRemaining > 0)
        {
            if (!ensureActivePlaylistItem())
            {
                outBuffer->clear(destPos, samplesRemaining);
                break;
            }

            const auto& item = playlistItems[activeState.itemIndex];
            const bool isAudio = (item.type == PlaylistItem::ItemType::AudioFile && playlistResampler != nullptr);

            int samplesFromItem = samplesRemaining;
            if (!activeState.infinite)
                samplesFromItem = juce::jmin(samplesFromItem, (int)std::ceil(activeState.samplesRemaining));

            const double chunkStartSample = activeState.itemTotalSamples - activeState.samplesRemaining;

            if (isAudio)
            {
                juce::AudioSourceChannelInfo chunkInfo(outBuffer, destPos, samplesFromItem);
                playlistResampler->getNextAudioBlock(chunkInfo);
            }
            else
            {
                outBuffer->clear(destPos, samplesFromItem);
            }

            bool finished = false;
            if (!activeState.infinite)
            {
                activeState.samplesRemaining -= samplesFromItem;
                finished = activeState.samplesRemaining <= 1.0;
            }

            const bool canCrossfade = (!activeState.infinite &&
                                       activeState.crossfadeSamples > 0.0 &&
                                       playlistItems.size() > 1);

            if (canCrossfade)
            {
                const double fadeLength = activeState.crossfadeSamples;
                const double fadeStartSample = juce::jmax(0.0, activeState.itemTotalSamples - fadeLength);
                const double chunkEndSample = chunkStartSample + samplesFromItem;

                if (chunkEndSample > fadeStartSample && fadeLength > 0.0)
                {
                    const double fadeChunkStart = juce::jmax(chunkStartSample, fadeStartSample);
                    const double fadeChunkEnd = juce::jmin(chunkEndSample, activeState.itemTotalSamples);
                    const int fadeSamples = juce::jmax(0, juce::roundToInt(fadeChunkEnd - fadeChunkStart));

                    if (fadeSamples > 0 && ensureUpcomingItemPrepared())
                    {
                        const int fadeStartOffset = juce::jmax(0, juce::roundToInt(fadeChunkStart - chunkStartSample));
                        const double fadePosStart = fadeChunkStart - fadeStartSample;
                        const double fadePosEnd = fadeChunkEnd - fadeStartSample;

                        const float fadeOutStart = (float)juce::jlimit(0.0, 1.0, 1.0 - (fadePosStart / fadeLength));
                        const float fadeOutEnd = (float)juce::jlimit(0.0, 1.0, 1.0 - (fadePosEnd / fadeLength));
                        const float fadeInStart = (float)juce::jlimit(0.0, 1.0, fadePosStart / fadeLength);
                        const float fadeInEnd = (float)juce::jlimit(0.0, 1.0, fadePosEnd / fadeLength);

                        ensureTempBuffer(outBuffer->getNumChannels(), fadeSamples);

                        for (int ch = 0; ch < outBuffer->getNumChannels(); ++ch)
                        {
                            outBuffer->applyGainRamp(ch,
                                                     destPos + fadeStartOffset,
                                                     fadeSamples,
                                                     fadeOutStart,
                                                     fadeOutEnd);
                        }

                        juce::AudioSourceChannelInfo fadeInfo(&crossfadeBuffer, 0, fadeSamples);
                        if (playlistItems[upcomingState.itemIndex].type == PlaylistItem::ItemType::AudioFile &&
                            upcomingResampler != nullptr)
                        {
                            upcomingResampler->getNextAudioBlock(fadeInfo);
                        }
                        else
                        {
                            crossfadeBuffer.clear();
                        }

                        for (int ch = 0; ch < crossfadeBuffer.getNumChannels(); ++ch)
                        {
                            crossfadeBuffer.applyGainRamp(ch, 0, fadeSamples, fadeInStart, fadeInEnd);
                        }

                        for (int ch = 0; ch < outBuffer->getNumChannels(); ++ch)
                        {
                            outBuffer->addFrom(ch,
                                               destPos + fadeStartOffset,
                                               crossfadeBuffer,
                                               ch,
                                               0,
                                               fadeSamples);
                        }

                        upcomingState.samplesRemaining = juce::jmax(0.0, upcomingState.samplesRemaining - fadeSamples);
                        crossfadeInProgress = true;
                    }
                }
            }

            if (finished && !activeState.infinite)
            {
                if (crossfadeInProgress)
                    promoteUpcomingItem();
                else
                    advanceToNextPlaylistItem();
            }

            destPos += samplesFromItem;
            samplesRemaining -= samplesFromItem;
        }
    }

    void ensureTempBuffer(int channels, int samples)
    {
        if (crossfadeBuffer.getNumChannels() != channels || crossfadeBuffer.getNumSamples() < samples)
            crossfadeBuffer.setSize(channels, samples, false, true, true);
    }

    void applyFadeOut(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        if (numSamples <= 0)
            return;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, startSample, numSamples, 1.0f, 0.0f);
    }

    void applyFadeIn(juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (numSamples <= 0)
            return;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, 0, numSamples, 0.0f, 1.0f);
    }

    juce::AudioFormatManager formatManager;

    bool playlistMode{ false };
    bool isPlaying{ false };
    float currentGain{ 1.0f };

    double loadedFileSampleRate{ 0.0 };
    double deviceSampleRate{ 44100.0 };
    int lastBlockSize{ 512 };

    juce::File loadedFile;

    std::vector<PlaylistItem> playlistItems;
    int playlistIndex{ -1 };

    std::unique_ptr<juce::AudioFormatReaderSource> playlistReader;
    std::unique_ptr<juce::ResamplingAudioSource>   playlistResampler;
    std::unique_ptr<juce::AudioFormatReaderSource> upcomingReader;
    std::unique_ptr<juce::ResamplingAudioSource>   upcomingResampler;

    ActivePlaylistState activeState;
    ActivePlaylistState upcomingState;
    bool upcomingStateValid{ false };
    bool crossfadeInProgress{ false };

    juce::AudioBuffer<float> crossfadeBuffer;
};
