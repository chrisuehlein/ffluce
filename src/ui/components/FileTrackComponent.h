#pragma once

#include "AudioTrackComponent.h"
#include "../../audio/FilePlayerAudioSource.h"
#include <vector>

class FileTrackComponent : public AudioTrackComponent
{
public:
    FileTrackComponent(FilePlayerAudioSource* src, FilePlayerAudioSource* streamingSrc = nullptr);
    
    void resized() override;
    
    juce::String getLoadedFilePath() const { return firstAudioPath; }
    const std::vector<FilePlayerAudioSource::PlaylistItem>& getPlaylistItems() const { return playlistItems; }
    void setPlaylistItems(const std::vector<FilePlayerAudioSource::PlaylistItem>& items);
    void loadAudioFile(const juce::File& file);

private:
    class PlaylistTableModel : public juce::TableListBoxModel
    {
    public:
        explicit PlaylistTableModel(FileTrackComponent& ownerIn) : owner(ownerIn) {}
        int getNumRows() override;
        void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                                                 juce::Component* existingComponentToUpdate) override;
        void selectedRowsChanged(int lastRowSelected) override;
    private:
        FileTrackComponent& owner;
    };

    class SequenceButtonsComponent : public juce::Component,
                                     private juce::Button::Listener
    {
    public:
        SequenceButtonsComponent(FileTrackComponent& ownerIn, int rowIndex);
        void resized() override;
        void buttonClicked(juce::Button*) override;
        void updateRow(int newRow);
    private:
        FileTrackComponent& owner;
        int row;
        juce::TextButton upButton { "Up" };
        juce::TextButton downButton { "Down" };
    };

    class PlaylistValueEditor : public juce::Label
    {
    public:
        PlaylistValueEditor(FileTrackComponent& ownerIn, int rowIndex);
        void updateRow(int newRow) { row = newRow; }
    protected:
        FileTrackComponent& owner;
        int row;
    };

    class DurationValueEditor : public PlaylistValueEditor
    {
    public:
        DurationValueEditor(FileTrackComponent& ownerIn, int rowIndex);
        void textWasEdited() override;
    };

    class CrossfadeValueEditor : public PlaylistValueEditor
    {
    public:
        CrossfadeValueEditor(FileTrackComponent& ownerIn, int rowIndex);
        void textWasEdited() override;
    };

    class RepetitionValueEditor : public PlaylistValueEditor
    {
    public:
        RepetitionValueEditor(FileTrackComponent& ownerIn, int rowIndex);
        void textWasEdited() override;
    };

    void buttonClicked(juce::Button* b) override;

    void showAddMenu();
    void addAudioItem();
    void addSilenceItem();
    void editSelectedItem();
    void removeSelectedItem();
    void playlistSelectionChanged(int newSelection);
    void updateButtonStates();
    void refreshPlaylist();
    void syncSources();
    void movePlaylistRowUp(int index);
    void movePlaylistRowDown(int index);
    void updatePlaylistDuration(int index, double newDuration);
    void updatePlaylistCrossfade(int index, double newCrossfade);
    void updatePlaylistRepetitions(int index, int newRepetitions);

    FilePlayerAudioSource* fileSource = nullptr;
    FilePlayerAudioSource* streamingFileSource = nullptr;

    PlaylistTableModel playlistModel;
    juce::TableListBox playlistTable;
    juce::TextButton addButton{ "Add" };
    juce::TextButton editButton{ "Edit" };
    juce::TextButton removeButton{ "Remove" };

    juce::Label playlistPlaceholder;

    juce::String firstAudioPath;
    std::vector<FilePlayerAudioSource::PlaylistItem> playlistItems;
    int selectedRow{-1};

    std::unique_ptr<juce::FileChooser> fileChooser;
};
