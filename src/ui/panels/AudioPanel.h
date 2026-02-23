#pragma once

#include <JuceHeader.h>
#include "../../audio/BinauralAudioSource.h"
#include "../../audio/FilePlayerAudioSource.h"
#include "../../audio/NoiseAudioSource.h"
#include "../components/BinauralTrackComponent.h"
#include "../components/FileTrackComponent.h"
#include "../components/NoiseTrackComponent.h"
#include <memory>
#include <vector>

class AudioPanel : public juce::Component
{
public:
    enum class TrackType
    {
        Binaural,
        File,
        Noise
    };

    struct TrackSnapshot
    {
        TrackType type{ TrackType::File };
        int laneWidth{ 320 };
        float gainValue{ 0.0f };
        bool muted{ false };
        bool solo{ false };

        double leftFrequency{ 70.0 };
        double rightFrequency{ 74.0 };
        int noiseType{ static_cast<int>(NoiseAudioSource::White) };

        std::vector<FilePlayerAudioSource::PlaylistItem> playlistItems;
    };

    AudioPanel();
    ~AudioPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void createDefaultTracks();
    void clearTracks();
    bool addTrack(TrackType type, int preferredWidth = 0);
    void removeTrack(int index);

    int getNumTracks() const;

    std::vector<juce::AudioSource*> getLocalAudioSources() const;
    std::vector<juce::AudioSource*> getStreamingAudioSources() const;

    std::vector<BinauralAudioSource*> getBinauralSources(bool streaming) const;
    std::vector<FilePlayerAudioSource*> getFileSources(bool streaming) const;
    std::vector<NoiseAudioSource*> getNoiseSources(bool streaming) const;

    std::vector<BinauralTrackComponent*> getBinauralTracks() const;
    std::vector<FileTrackComponent*> getFileTracks() const;
    std::vector<NoiseTrackComponent*> getNoiseTracks() const;
    void updateTrackMeters(float leftLevel, float rightLevel);

    juce::Array<TrackSnapshot> createSnapshots() const;
    void loadFromSnapshots(const juce::Array<TrackSnapshot>& snapshots, bool useDefaultIfEmpty);

    void syncLocalSourcesFromUI();
    void syncStreamingFromLocal();
    void clearAllFileTrackPlaylists();
    void openAddTrackMenu();

    static juce::String trackTypeToString(TrackType type);
    static TrackType trackTypeFromString(const juce::String& typeText, TrackType fallback);

    std::function<void()> onTracksChanged;

private:
    class TrackLaneComponent;
    struct TrackEntry
    {
        TrackType type{ TrackType::File };
        int preferredWidth{ 320 };

        std::unique_ptr<BinauralAudioSource> localBinaural;
        std::unique_ptr<BinauralAudioSource> streamingBinaural;
        std::unique_ptr<FilePlayerAudioSource> localFile;
        std::unique_ptr<FilePlayerAudioSource> streamingFile;
        std::unique_ptr<NoiseAudioSource> localNoise;
        std::unique_ptr<NoiseAudioSource> streamingNoise;

        std::unique_ptr<AudioTrackComponent> trackComponent;
        std::unique_ptr<TrackLaneComponent> laneComponent;
    };

    TrackEntry* getTrackEntry(int index);
    const TrackEntry* getTrackEntry(int index) const;

    void rebuildLaneLayout();
    void handleSoloChanged(AudioTrackComponent* changedTrack);
    void setupCommonCallbacks(TrackEntry& entry);
    void showAddTrackMenu();
    void notifyTracksChanged();

    std::vector<TrackEntry> tracks;

    juce::Viewport trackViewport;
    juce::Component laneContainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPanel)
};
