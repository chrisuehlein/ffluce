#include "AudioPanel.h"

namespace
{
    constexpr int kLaneSpacing = 8;
    constexpr int kLaneMinWidth = 190;
    constexpr int kFileLaneDefaultWidth = 440;
    constexpr int kNoiseLaneDefaultWidth = 260;
    constexpr int kBinauralLaneDefaultWidth = 260;
    constexpr int kResizeHandleWidth = 10;
    constexpr int kHandlePadding = 10;
}

class AudioPanel::TrackLaneComponent : public juce::Component
{
public:
    TrackLaneComponent(AudioTrackComponent& trackComp, TrackType laneType)
        : track(trackComp), type(laneType)
    {
        removeButton.setButtonText("Remove");
        removeButton.onClick = [this]()
        {
            if (onRemoveRequested)
                onRemoveRequested();
        };
        addAndMakeVisible(removeButton);
        addAndMakeVisible(track);
    }

    void setPreferredWidth(int newWidth)
    {
        preferredWidth = juce::jmax(kLaneMinWidth, newWidth);
    }

    int getPreferredWidth() const
    {
        return preferredWidth;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(12.0f);
        g.drawText(AudioPanel::trackTypeToString(type) + " Track",
                   10,
                   4,
                   juce::jmax(0, getContentBounds().getWidth() - 110),
                   20,
                   juce::Justification::centredLeft,
                   false);

        auto handleReserve = getHandleReserveBounds();
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(handleReserve);

        auto handleArea = getResizeHandleBounds();
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillRect(handleArea.reduced(3, 10));
    }

    void resized() override
    {
        auto area = getContentBounds();
        auto header = area.removeFromTop(26);
        removeButton.setBounds(header.removeFromRight(95).reduced(2, 2));

        auto body = area.reduced(6, 2);
        track.setBounds(body);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool onHandle = getResizeHandleBounds().contains(e.getPosition());
        setMouseCursor(onHandle ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartX = e.getPosition().getX();
        startWidth = preferredWidth;
        dragging = getResizeHandleBounds().contains(e.getPosition());
        if (dragging)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragging = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging)
            return;

        const int delta = e.getPosition().getX() - dragStartX;
        const int nextWidth = juce::jmax(kLaneMinWidth, startWidth + delta);
        if (nextWidth == preferredWidth)
            return;

        preferredWidth = nextWidth;
        if (onResizeRequested)
            onResizeRequested(preferredWidth);
    }

    std::function<void()> onRemoveRequested;
    std::function<void(int)> onResizeRequested;

private:
    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().withTrimmedRight(kResizeHandleWidth + kHandlePadding);
    }

    juce::Rectangle<int> getHandleReserveBounds() const
    {
        return getLocalBounds().removeFromRight(kResizeHandleWidth + kHandlePadding);
    }

    juce::Rectangle<int> getResizeHandleBounds() const
    {
        return getLocalBounds().removeFromRight(kResizeHandleWidth);
    }

    AudioTrackComponent& track;
    TrackType type;
    juce::TextButton removeButton;

    int preferredWidth{ kFileLaneDefaultWidth };
    int dragStartX{ 0 };
    int startWidth{ kFileLaneDefaultWidth };
    bool dragging{ false };
};

AudioPanel::AudioPanel()
{
    trackViewport.setViewedComponent(&laneContainer, false);
    trackViewport.setScrollBarsShown(false, true);
    addAndMakeVisible(trackViewport);

    createDefaultTracks();
}

AudioPanel::~AudioPanel() = default;

void AudioPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
}

void AudioPanel::resized()
{
    trackViewport.setBounds(getLocalBounds().reduced(6));
    rebuildLaneLayout();
}

void AudioPanel::createDefaultTracks()
{
    clearTracks();

    addTrack(TrackType::File, kFileLaneDefaultWidth);
    addTrack(TrackType::Noise, kNoiseLaneDefaultWidth);
    addTrack(TrackType::Binaural, kBinauralLaneDefaultWidth);

    auto noiseTracks = getNoiseTracks();
    if (!noiseTracks.empty())
        noiseTracks.front()->setMuteState(true);

    auto binauralTracks = getBinauralTracks();
    if (!binauralTracks.empty())
        binauralTracks.front()->setGain(0.0f);

    syncStreamingFromLocal();
    rebuildLaneLayout();
}

void AudioPanel::clearTracks()
{
    for (auto& entry : tracks)
    {
        if (entry.laneComponent)
            laneContainer.removeChildComponent(entry.laneComponent.get());
    }

    tracks.clear();
    rebuildLaneLayout();
}

bool AudioPanel::addTrack(TrackType type, int preferredWidth)
{
    TrackEntry entry;
    entry.type = type;
    const int fallbackWidth = (type == TrackType::File ? kFileLaneDefaultWidth
                                                       : (type == TrackType::Noise ? kNoiseLaneDefaultWidth : kBinauralLaneDefaultWidth));
    entry.preferredWidth = juce::jmax(kLaneMinWidth, preferredWidth > 0 ? preferredWidth : fallbackWidth);

    switch (type)
    {
        case TrackType::Binaural:
        {
            entry.localBinaural = std::make_unique<BinauralAudioSource>();
            entry.streamingBinaural = std::make_unique<BinauralAudioSource>();
            auto track = std::make_unique<BinauralTrackComponent>(entry.localBinaural.get(), entry.streamingBinaural.get());
            entry.trackComponent = std::move(track);
            break;
        }
        case TrackType::File:
        {
            entry.localFile = std::make_unique<FilePlayerAudioSource>();
            entry.streamingFile = std::make_unique<FilePlayerAudioSource>();
            auto track = std::make_unique<FileTrackComponent>(entry.localFile.get(), entry.streamingFile.get());
            entry.trackComponent = std::move(track);
            break;
        }
        case TrackType::Noise:
        {
            entry.localNoise = std::make_unique<NoiseAudioSource>();
            entry.streamingNoise = std::make_unique<NoiseAudioSource>();
            auto track = std::make_unique<NoiseTrackComponent>(entry.localNoise.get(), entry.streamingNoise.get());
            entry.trackComponent = std::move(track);
            break;
        }
        default:
            return false;
    }

    if (!entry.trackComponent)
        return false;

    entry.trackComponent->setTrackTitleVisible(false);
    setupCommonCallbacks(entry);

    entry.laneComponent = std::make_unique<TrackLaneComponent>(*entry.trackComponent, type);
    entry.laneComponent->setPreferredWidth(entry.preferredWidth);

    const int index = static_cast<int>(tracks.size());
    entry.laneComponent->onRemoveRequested = [this, index]()
    {
        removeTrack(index);
    };
    entry.laneComponent->onResizeRequested = [this, index](int newWidth)
    {
        if (auto* e = getTrackEntry(index))
            e->preferredWidth = newWidth;
        rebuildLaneLayout();
    };

    laneContainer.addAndMakeVisible(entry.laneComponent.get());
    tracks.push_back(std::move(entry));

    syncStreamingFromLocal();
    rebuildLaneLayout();
    notifyTracksChanged();
    return true;
}

void AudioPanel::removeTrack(int index)
{
    if (index < 0 || index >= static_cast<int>(tracks.size()))
        return;

    laneContainer.removeChildComponent(tracks[(size_t)index].laneComponent.get());
    tracks.erase(tracks.begin() + index);

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        if (!tracks[(size_t)i].laneComponent)
            continue;

        tracks[(size_t)i].laneComponent->onRemoveRequested = [this, i]()
        {
            removeTrack(i);
        };
        tracks[(size_t)i].laneComponent->onResizeRequested = [this, i](int newWidth)
        {
            if (auto* e = getTrackEntry(i))
                e->preferredWidth = newWidth;
            rebuildLaneLayout();
        };
    }

    rebuildLaneLayout();
    notifyTracksChanged();
}

int AudioPanel::getNumTracks() const
{
    return static_cast<int>(tracks.size());
}

std::vector<juce::AudioSource*> AudioPanel::getLocalAudioSources() const
{
    std::vector<juce::AudioSource*> sources;
    sources.reserve(tracks.size());

    for (const auto& entry : tracks)
    {
        if (entry.localBinaural)
            sources.push_back(entry.localBinaural.get());
        else if (entry.localFile)
            sources.push_back(entry.localFile.get());
        else if (entry.localNoise)
            sources.push_back(entry.localNoise.get());
    }

    return sources;
}

std::vector<juce::AudioSource*> AudioPanel::getStreamingAudioSources() const
{
    std::vector<juce::AudioSource*> sources;
    sources.reserve(tracks.size());

    for (const auto& entry : tracks)
    {
        if (entry.streamingBinaural)
            sources.push_back(entry.streamingBinaural.get());
        else if (entry.streamingFile)
            sources.push_back(entry.streamingFile.get());
        else if (entry.streamingNoise)
            sources.push_back(entry.streamingNoise.get());
    }

    return sources;
}

std::vector<BinauralAudioSource*> AudioPanel::getBinauralSources(bool streaming) const
{
    std::vector<BinauralAudioSource*> sources;
    for (const auto& entry : tracks)
    {
        if (streaming)
        {
            if (entry.streamingBinaural)
                sources.push_back(entry.streamingBinaural.get());
        }
        else if (entry.localBinaural)
        {
            sources.push_back(entry.localBinaural.get());
        }
    }
    return sources;
}

std::vector<FilePlayerAudioSource*> AudioPanel::getFileSources(bool streaming) const
{
    std::vector<FilePlayerAudioSource*> sources;
    for (const auto& entry : tracks)
    {
        if (streaming)
        {
            if (entry.streamingFile)
                sources.push_back(entry.streamingFile.get());
        }
        else if (entry.localFile)
        {
            sources.push_back(entry.localFile.get());
        }
    }
    return sources;
}

std::vector<NoiseAudioSource*> AudioPanel::getNoiseSources(bool streaming) const
{
    std::vector<NoiseAudioSource*> sources;
    for (const auto& entry : tracks)
    {
        if (streaming)
        {
            if (entry.streamingNoise)
                sources.push_back(entry.streamingNoise.get());
        }
        else if (entry.localNoise)
        {
            sources.push_back(entry.localNoise.get());
        }
    }
    return sources;
}

std::vector<BinauralTrackComponent*> AudioPanel::getBinauralTracks() const
{
    std::vector<BinauralTrackComponent*> result;
    for (const auto& entry : tracks)
    {
        if (auto* t = dynamic_cast<BinauralTrackComponent*>(entry.trackComponent.get()))
            result.push_back(t);
    }
    return result;
}

std::vector<FileTrackComponent*> AudioPanel::getFileTracks() const
{
    std::vector<FileTrackComponent*> result;
    for (const auto& entry : tracks)
    {
        if (auto* t = dynamic_cast<FileTrackComponent*>(entry.trackComponent.get()))
            result.push_back(t);
    }
    return result;
}

std::vector<NoiseTrackComponent*> AudioPanel::getNoiseTracks() const
{
    std::vector<NoiseTrackComponent*> result;
    for (const auto& entry : tracks)
    {
        if (auto* t = dynamic_cast<NoiseTrackComponent*>(entry.trackComponent.get()))
            result.push_back(t);
    }
    return result;
}

void AudioPanel::updateTrackMeters(float leftLevel, float rightLevel)
{
    const float baseLevel = juce::jmax(0.0f, juce::jmax(leftLevel, rightLevel));

    for (auto& entry : tracks)
    {
        if (!entry.trackComponent)
            continue;

        float level = 0.0f;

        if (entry.localBinaural)
        {
            level = baseLevel * juce::jmax(0.0f, entry.localBinaural->getGain());
        }
        else if (entry.localFile)
        {
            if (entry.localFile->isLoaded())
                level = baseLevel * juce::jmax(0.0f, entry.localFile->getGain());
        }
        else if (entry.localNoise)
        {
            if (!entry.localNoise->isMuted())
                level = baseLevel * juce::jmax(0.0f, entry.localNoise->getGain());
        }

        entry.trackComponent->setMeterLevel(level);
    }
}

juce::Array<AudioPanel::TrackSnapshot> AudioPanel::createSnapshots() const
{
    juce::Array<TrackSnapshot> snapshots;

    for (const auto& entry : tracks)
    {
        TrackSnapshot snapshot;
        snapshot.type = entry.type;
        snapshot.laneWidth = entry.preferredWidth;

        if (entry.trackComponent)
        {
            snapshot.gainValue = entry.trackComponent->getGain();
            snapshot.muted = entry.trackComponent->isMuted();
            snapshot.solo = entry.trackComponent->isSolo();
        }

        if (auto* binaural = dynamic_cast<BinauralTrackComponent*>(entry.trackComponent.get()))
        {
            snapshot.leftFrequency = binaural->getLeftFrequency();
            snapshot.rightFrequency = binaural->getRightFrequency();
        }
        else if (auto* fileTrack = dynamic_cast<FileTrackComponent*>(entry.trackComponent.get()))
        {
            snapshot.playlistItems = fileTrack->getPlaylistItems();
        }
        else if (entry.localNoise != nullptr)
        {
            snapshot.noiseType = static_cast<int>(entry.localNoise->getNoiseType());
        }

        snapshots.add(snapshot);
    }

    return snapshots;
}

void AudioPanel::loadFromSnapshots(const juce::Array<TrackSnapshot>& snapshots, bool useDefaultIfEmpty)
{
    clearTracks();

    if (snapshots.isEmpty())
    {
        if (useDefaultIfEmpty)
            createDefaultTracks();
        return;
    }

    for (const auto& snapshot : snapshots)
        addTrack(snapshot.type, snapshot.laneWidth);

    const int count = juce::jmin(getNumTracks(), snapshots.size());
    for (int i = 0; i < count; ++i)
    {
        auto* entry = getTrackEntry(i);
        if (!entry || !entry->trackComponent)
            continue;

        const auto& snapshot = snapshots.getReference(i);
        entry->preferredWidth = juce::jmax(kLaneMinWidth, snapshot.laneWidth);
        if (entry->laneComponent)
            entry->laneComponent->setPreferredWidth(entry->preferredWidth);

        entry->trackComponent->setGain(snapshot.gainValue);
        entry->trackComponent->setMuteState(snapshot.muted);
        entry->trackComponent->setSoloState(snapshot.solo);

        if (auto* binaural = dynamic_cast<BinauralTrackComponent*>(entry->trackComponent.get()))
        {
            binaural->setLeftFrequency(snapshot.leftFrequency);
            binaural->setRightFrequency(snapshot.rightFrequency);
        }
        else if (auto* fileTrack = dynamic_cast<FileTrackComponent*>(entry->trackComponent.get()))
        {
            fileTrack->setPlaylistItems(snapshot.playlistItems);
        }
        else if (auto* noiseTrack = dynamic_cast<NoiseTrackComponent*>(entry->trackComponent.get()))
        {
            noiseTrack->setNoiseType(static_cast<NoiseAudioSource::NoiseType>(snapshot.noiseType));
        }
    }

    syncStreamingFromLocal();
    rebuildLaneLayout();
    notifyTracksChanged();
}

void AudioPanel::syncLocalSourcesFromUI()
{
    for (auto& entry : tracks)
    {
        auto* track = entry.trackComponent.get();
        if (track == nullptr)
            continue;

        const float unmutedGain = track->getActualGain();
        const float renderGain = track->isMuted() ? 0.0f : unmutedGain;

        if (entry.localBinaural)
        {
            if (auto* binauralTrack = dynamic_cast<BinauralTrackComponent*>(track))
            {
                entry.localBinaural->setLeftFrequency(binauralTrack->getLeftFrequency());
                entry.localBinaural->setRightFrequency(binauralTrack->getRightFrequency());
            }

            entry.localBinaural->setGain(renderGain);
        }

        if (entry.localFile)
        {
            if (auto* fileTrack = dynamic_cast<FileTrackComponent*>(track))
                entry.localFile->setPlaylist(fileTrack->getPlaylistItems());

            entry.localFile->setGain(renderGain);
            entry.localFile->setPosition(0.0);
        }

        if (entry.localNoise)
        {
            entry.localNoise->setGain(unmutedGain);
            entry.localNoise->setMuted(track->isMuted());
        }
    }
}

void AudioPanel::syncStreamingFromLocal()
{
    for (auto& entry : tracks)
    {
        if (entry.localBinaural && entry.streamingBinaural)
        {
            entry.streamingBinaural->setLeftFrequency(entry.localBinaural->getLeftFrequency());
            entry.streamingBinaural->setRightFrequency(entry.localBinaural->getRightFrequency());
            entry.streamingBinaural->setGain(entry.localBinaural->getGain());
        }

        if (entry.localFile && entry.streamingFile)
        {
            entry.streamingFile->setPlaylist(entry.localFile->getPlaylist());
            entry.streamingFile->setGain(entry.localFile->getGain());
            entry.streamingFile->start();
        }

        if (entry.localNoise && entry.streamingNoise)
        {
            entry.streamingNoise->setNoiseType(entry.localNoise->getNoiseType());
            entry.streamingNoise->setGain(entry.localNoise->getGain());
            entry.streamingNoise->setMuted(entry.localNoise->isMuted());
        }
    }
}

void AudioPanel::clearAllFileTrackPlaylists()
{
    for (auto& entry : tracks)
    {
        if (auto* fileTrack = dynamic_cast<FileTrackComponent*>(entry.trackComponent.get()))
            fileTrack->clearPlaylist();
    }
}

juce::String AudioPanel::trackTypeToString(TrackType type)
{
    switch (type)
    {
        case TrackType::Binaural: return "Binaural";
        case TrackType::File:     return "File";
        case TrackType::Noise:    return "Noise";
        default:                  return "Unknown";
    }
}

AudioPanel::TrackType AudioPanel::trackTypeFromString(const juce::String& typeText, TrackType fallback)
{
    const juce::String lowered = typeText.trim().toLowerCase();
    if (lowered == "binaural")
        return TrackType::Binaural;
    if (lowered == "file" || lowered == "playlist")
        return TrackType::File;
    if (lowered == "noise")
        return TrackType::Noise;
    return fallback;
}

AudioPanel::TrackEntry* AudioPanel::getTrackEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(tracks.size()))
        return nullptr;
    return &tracks[(size_t)index];
}

const AudioPanel::TrackEntry* AudioPanel::getTrackEntry(int index) const
{
    if (index < 0 || index >= static_cast<int>(tracks.size()))
        return nullptr;
    return &tracks[(size_t)index];
}

void AudioPanel::rebuildLaneLayout()
{
    const int viewportWidth = juce::jmax(100, trackViewport.getWidth());
    const int viewportHeight = juce::jmax(100, trackViewport.getHeight() - 12);
    int x = 0;

    for (auto& entry : tracks)
    {
        if (!entry.laneComponent)
            continue;

        const int laneWidth = juce::jmax(kLaneMinWidth, entry.preferredWidth);
        entry.laneComponent->setPreferredWidth(laneWidth);
        entry.laneComponent->setBounds(x, 0, laneWidth, viewportHeight);
        x += laneWidth + kLaneSpacing;
    }

    if (x > 0)
        x -= kLaneSpacing;

    laneContainer.setSize(juce::jmax(viewportWidth, x), viewportHeight);
    laneContainer.repaint();
}

void AudioPanel::handleSoloChanged(AudioTrackComponent* changedTrack)
{
    if (changedTrack == nullptr || !changedTrack->isSolo())
        return;

    for (auto& entry : tracks)
    {
        if (entry.trackComponent.get() != changedTrack && entry.trackComponent)
            entry.trackComponent->setSoloState(false);
    }
}

void AudioPanel::setupCommonCallbacks(TrackEntry& entry)
{
    if (!entry.trackComponent)
        return;

    auto previousSoloCallback = entry.trackComponent->onSoloChanged;
    entry.trackComponent->onSoloChanged = [this, previousSoloCallback](AudioTrackComponent* changedTrack)
    {
        if (previousSoloCallback)
            previousSoloCallback(changedTrack);
        handleSoloChanged(changedTrack);
    };

    auto previousGainCallback = entry.trackComponent->onGainChanged;
    entry.trackComponent->onGainChanged = [this, previousGainCallback](AudioTrackComponent* track, float gain)
    {
        if (previousGainCallback)
            previousGainCallback(track, gain);
        syncStreamingFromLocal();
    };
}

void AudioPanel::showAddTrackMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "File Track");
    menu.addItem(2, "Noise Track");
    menu.addItem(3, "Binaural Track");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result)
    {
        if (result == 1)
            addTrack(TrackType::File, kFileLaneDefaultWidth);
        else if (result == 2)
            addTrack(TrackType::Noise, kNoiseLaneDefaultWidth);
        else if (result == 3)
            addTrack(TrackType::Binaural, kBinauralLaneDefaultWidth);
    });
}

void AudioPanel::openAddTrackMenu()
{
    showAddTrackMenu();
}

void AudioPanel::notifyTracksChanged()
{
    if (onTracksChanged)
        onTracksChanged();
}
