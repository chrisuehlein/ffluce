#include "FileTrackComponent.h"
#include <JuceHeader.h>

namespace
{
    class PlaylistItemEditor  : public juce::Component,
                                private juce::Button::Listener
    {
    public:
        PlaylistItemEditor(const FilePlayerAudioSource::PlaylistItem& initialItem,
                           bool isAudioItem,
                           std::function<void (bool, FilePlayerAudioSource::PlaylistItem)> onCloseIn)
            : item(initialItem),
              isAudio(isAudioItem),
              onClose(std::move(onCloseIn))
        {
            titleLabel.setText(isAudio ? "Audio Clip Settings" : "Silence Settings", juce::dontSendNotification);
            titleLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(titleLabel);

            if (isAudio)
            {
                addAndMakeVisible(repsLabel);
                repsLabel.setText("Repetitions (0 = infinite)", juce::dontSendNotification);
                addAndMakeVisible(repsEditor);
                repsEditor.setText(juce::String(item.repetitions));

                addAndMakeVisible(durationLabel);
                durationLabel.setText("Target duration (seconds, optional)", juce::dontSendNotification);
                addAndMakeVisible(durationEditor);
                durationEditor.setText(item.targetDurationSeconds > 0.0 ? juce::String(item.targetDurationSeconds, 2) : juce::String());
            }
            else
            {
                addAndMakeVisible(durationLabel);
                durationLabel.setText("Silence duration (seconds)", juce::dontSendNotification);
                addAndMakeVisible(durationEditor);
                durationEditor.setText(item.targetDurationSeconds > 0.0 ? juce::String(item.targetDurationSeconds, 2) : juce::String("10.0"));
            }

            addAndMakeVisible(crossfadeLabel);
            crossfadeLabel.setText("Crossfade seconds", juce::dontSendNotification);
            addAndMakeVisible(crossfadeEditor);
            crossfadeEditor.setText(juce::String(item.crossfadeSeconds, 2));

            okButton.setButtonText("OK");
            cancelButton.setButtonText("Cancel");
            okButton.addListener(this);
            cancelButton.addListener(this);
            addAndMakeVisible(okButton);
            addAndMakeVisible(cancelButton);

            setSize(300, isAudio ? 210 : 170);
        }

    private:
        void resized() override
        {
            auto area = getLocalBounds().reduced(10);
            titleLabel.setBounds(area.removeFromTop(24));

            auto placeEditor = [&area](juce::Label& label, juce::TextEditor& editor)
            {
                label.setBounds(area.removeFromTop(18));
                editor.setBounds(area.removeFromTop(24));
                area.removeFromTop(6);
            };

            if (isAudio)
            {
                placeEditor(repsLabel, repsEditor);
                placeEditor(durationLabel, durationEditor);
            }
            else
            {
                placeEditor(durationLabel, durationEditor);
            }

            placeEditor(crossfadeLabel, crossfadeEditor);

            auto buttonRow = area.removeFromBottom(26);
            okButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2).reduced(4));
            cancelButton.setBounds(buttonRow.reduced(4));
        }

        void buttonClicked(juce::Button* b) override
        {
            const bool accepted = (b == &okButton);
            if (accepted)
            {
                if (isAudio)
                {
                    item.repetitions = repsEditor.getText().getIntValue();
                    item.targetDurationSeconds = durationEditor.getText().trim().isEmpty() ? 0.0 : durationEditor.getText().getDoubleValue();
                }
                else
                {
                    item.targetDurationSeconds = juce::jmax(0.0, durationEditor.getText().getDoubleValue());
                    item.displayName = "Silence (" + juce::String(item.targetDurationSeconds, 1) + "s)";
                }

                item.crossfadeSeconds = juce::jmax(0.0, crossfadeEditor.getText().getDoubleValue());
            }

            if (auto* box = dynamic_cast<juce::CallOutBox*>(getParentComponent()))
                box->dismiss();

            if (onClose)
                onClose(accepted, item);
        }

        FilePlayerAudioSource::PlaylistItem item;
        bool isAudio{ false };
        std::function<void (bool, FilePlayerAudioSource::PlaylistItem)> onClose;

        juce::Label titleLabel;

        juce::Label repsLabel;
        juce::TextEditor repsEditor;

        juce::Label durationLabel;
        juce::TextEditor durationEditor;

        juce::Label crossfadeLabel;
        juce::TextEditor crossfadeEditor;

        juce::TextButton okButton, cancelButton;
    };

    void showPlaylistEditor(juce::Component& anchor,
                            const FilePlayerAudioSource::PlaylistItem& startItem,
                            bool isAudio,
                            std::function<void (bool, FilePlayerAudioSource::PlaylistItem)> callback)
    {
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(new PlaylistItemEditor(startItem, isAudio, std::move(callback))),
                                               anchor.getScreenBounds(),
                                               &anchor);
    }
}

FileTrackComponent::FileTrackComponent(FilePlayerAudioSource* src, FilePlayerAudioSource* streamingSrc)
    : AudioTrackComponent("File Track"),
      fileSource(src),
      streamingFileSource(streamingSrc),
      playlistModel(*this)
{
    trackTitle.setJustificationType(juce::Justification::centredLeft);

    playlistTable.setModel(&playlistModel);
    playlistTable.setRowHeight(24);
    playlistTable.setMultipleSelectionEnabled(false);
    playlistTable.setColour(juce::TableListBox::backgroundColourId, juce::Colour(28, 28, 28).withAlpha(0.9f));
    playlistTable.setColour(juce::TableListBox::outlineColourId, juce::Colours::darkgrey);
    playlistTable.setColour(juce::TableListBox::textColourId, juce::Colours::white);
    playlistTable.setHeaderHeight(24);
    playlistTable.setClickingTogglesRowSelection(true);
    playlistTable.setOutlineThickness(1);
    auto& header = playlistTable.getHeader();
    header.addColumn("Seq", 1, 70);
    header.addColumn("Clip", 2, 180);
    header.addColumn("Type", 3, 80);
    header.addColumn("Duration", 4, 110);
    header.addColumn("Repeats", 5, 90);
    header.addColumn("Crossfade", 6, 100);
    addAndMakeVisible(playlistTable);

    playlistPlaceholder.setText("Add playlist entries to begin", juce::dontSendNotification);
    playlistPlaceholder.setJustificationType(juce::Justification::centred);
    playlistPlaceholder.setColour(juce::Label::textColourId, juce::Colours::lightgrey.withAlpha(0.8f));
    playlistPlaceholder.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(playlistPlaceholder);

    auto configureButton = [this](juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText(text);
        button.addListener(this);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(100, 120, 140));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(button);
    };

    configureButton(addButton, "Add...");
    configureButton(editButton, "Edit");
    configureButton(removeButton, "Remove");

    onGainChanged = [this](AudioTrackComponent*, float gain) {
        const float actualGain = this->isMuted() ? 0.0f : gain;
        if (fileSource)          fileSource->setGain(actualGain);
        if (streamingFileSource) streamingFileSource->setGain(actualGain);
    };

    if (trackMeter)
        trackMeter->setVisible(false);

    if (fileSource != nullptr)
        playlistItems = fileSource->getPlaylist();

    refreshPlaylist();
}

void FileTrackComponent::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto titleArea = bounds.removeFromTop(24);
    trackTitle.setBounds(titleArea);

    bounds.removeFromTop(4);

    const int totalWidth = bounds.getWidth();
    const int playlistMinWidth = 220;
    const int defaultRightWidth = juce::jmax(160, totalWidth / 3);
    int rightColumnWidth = defaultRightWidth;
    if (totalWidth - rightColumnWidth < playlistMinWidth)
        rightColumnWidth = juce::jmax(160, totalWidth - playlistMinWidth);
    if (rightColumnWidth < 140)
        rightColumnWidth = juce::jmin(defaultRightWidth, totalWidth);
    auto rightArea = bounds.removeFromRight(juce::jlimit(140, totalWidth, rightColumnWidth));
    auto leftArea = bounds;

    auto buttonRow = leftArea.removeFromTop(28);
    const int buttonSpacing = 6;
    const int buttonWidth = juce::jmax(70, (buttonRow.getWidth() - buttonSpacing * 2) / 3);
    addButton.setBounds(buttonRow.removeFromLeft(buttonWidth));
    buttonRow.removeFromLeft(buttonSpacing);
    editButton.setBounds(buttonRow.removeFromLeft(buttonWidth));
    buttonRow.removeFromLeft(buttonSpacing);
    removeButton.setBounds(buttonRow.removeFromLeft(buttonWidth));

    leftArea.removeFromTop(6);
    playlistTable.setBounds(leftArea);
    playlistPlaceholder.setBounds(leftArea);
    playlistPlaceholder.toFront(false);

    auto sliderArea = rightArea.reduced(6);
    auto muteSoloArea = sliderArea.removeFromBottom(32);
    auto muteBounds = muteSoloArea.removeFromLeft((muteSoloArea.getWidth() - 6) / 2);
    muteButton.setBounds(muteBounds.reduced(2));
    muteSoloArea.removeFromLeft(6);
    soloButton.setBounds(muteSoloArea.reduced(2));

    auto valueArea = sliderArea.removeFromBottom(24);
    faderValueLabel.setBounds(valueArea);

    const int sliderWidth = juce::jmin(56, sliderArea.getWidth());
    juce::Rectangle<int> sliderBounds(sliderArea.getCentreX() - sliderWidth / 2,
                                      sliderArea.getY(),
                                      sliderWidth,
                                      sliderArea.getHeight());
    fader.setBounds(sliderBounds);

    for (auto* label : dbScaleLabels)
        if (label != nullptr)
            label->setVisible(false);
}


void FileTrackComponent::setPlaylistItems(const std::vector<FilePlayerAudioSource::PlaylistItem>& items)
{
    playlistItems = items;
    selectedRow = playlistItems.empty() ? -1 : 0;
    refreshPlaylist();
}

void FileTrackComponent::loadAudioFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    FilePlayerAudioSource::PlaylistItem single = FilePlayerAudioSource::PlaylistItem::createAudio(file, -1, 0.0, 0.0);
    playlistItems.clear();
    playlistItems.push_back(single);
    refreshPlaylist();
}

int FileTrackComponent::PlaylistTableModel::getNumRows()
{
    return (int)owner.playlistItems.size();
}

void FileTrackComponent::PlaylistTableModel::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colour(70, 90, 140).withAlpha(0.8f));
    else if (rowNumber % 2)
        g.fillAll(juce::Colour(40, 40, 40).withAlpha(0.6f));
    else
        g.fillAll(juce::Colour(30, 30, 30).withAlpha(0.6f));
}

void FileTrackComponent::PlaylistTableModel::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= owner.playlistItems.size())
        return;

    const auto& item = owner.playlistItems[(size_t)rowNumber];
    juce::String text;

    switch (columnId)
    {
        case 1:
            return;
        case 2:
            text = item.displayName;
            break;
        case 3:
            text = (item.type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile) ? "Audio" : "Silence";
            break;
        case 4:
        case 5:
        case 6:
            return;
        default:
            break;
    }

    g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont(13.0f);
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void FileTrackComponent::PlaylistTableModel::selectedRowsChanged(int lastRowSelected)
{
    owner.playlistSelectionChanged(lastRowSelected);
}

juce::Component* FileTrackComponent::PlaylistTableModel::refreshComponentForCell(int rowNumber, int columnId, bool, juce::Component* existingComponentToUpdate)
{
    if (rowNumber < 0 || rowNumber >= owner.playlistItems.size())
        return existingComponentToUpdate;

    if (columnId == 1)
    {
        auto* buttons = dynamic_cast<FileTrackComponent::SequenceButtonsComponent*>(existingComponentToUpdate);
        if (buttons == nullptr)
            buttons = new FileTrackComponent::SequenceButtonsComponent(owner, rowNumber);
        else
            buttons->updateRow(rowNumber);
        return buttons;
    }

    if (columnId == 4)
    {
        auto* editor = dynamic_cast<FileTrackComponent::DurationValueEditor*>(existingComponentToUpdate);
        if (editor == nullptr)
            editor = new FileTrackComponent::DurationValueEditor(owner, rowNumber);
        else
            editor->updateRow(rowNumber);

        const auto& item = owner.playlistItems[(size_t)rowNumber];
        editor->setText(item.targetDurationSeconds > 0.0 ? juce::String(item.targetDurationSeconds, 1) : juce::String("-"), juce::dontSendNotification);
        return editor;
    }

    if (columnId == 5)
    {
        auto* editor = dynamic_cast<FileTrackComponent::RepetitionValueEditor*>(existingComponentToUpdate);
        if (editor == nullptr)
            editor = new FileTrackComponent::RepetitionValueEditor(owner, rowNumber);
        else
            editor->updateRow(rowNumber);

        const auto& item = owner.playlistItems[(size_t)rowNumber];
        juce::String text = (item.repetitions > 0) ? juce::String(item.repetitions) : juce::String("loop");
        editor->setText(text, juce::dontSendNotification);
        return editor;
    }

    if (columnId == 6)
    {
        auto* editor = dynamic_cast<FileTrackComponent::CrossfadeValueEditor*>(existingComponentToUpdate);
        if (editor == nullptr)
            editor = new FileTrackComponent::CrossfadeValueEditor(owner, rowNumber);
        else
            editor->updateRow(rowNumber);

        const auto& item = owner.playlistItems[(size_t)rowNumber];
        editor->setText(juce::String(item.crossfadeSeconds, 1), juce::dontSendNotification);
        return editor;
    }

    return existingComponentToUpdate;
}


void FileTrackComponent::buttonClicked(juce::Button* b)
{
    AudioTrackComponent::buttonClicked(b);

    if (b == &addButton)
        showAddMenu();
    else if (b == &editButton)
        editSelectedItem();
    else if (b == &removeButton)
        removeSelectedItem();
}

void FileTrackComponent::showAddMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Add audio file...");
    menu.addItem(2, "Add silence gap...");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int choice)
    {
        if (choice == 1)      addAudioItem();
        else if (choice == 2) addSilenceItem();
    });
}

void FileTrackComponent::addAudioItem()
{
    fileChooser.reset(new juce::FileChooser("Select Audio File",
                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                            "*.wav;*.mp3;*.aiff;*.ogg;*.flac"));

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
    {
        juce::File selectedFile = chooser.getResult();
        if (!selectedFile.existsAsFile())
            return;

        playlistItems.push_back(FilePlayerAudioSource::PlaylistItem::createAudio(selectedFile, 1, 0.0, 2.0));
        selectedRow = (int)playlistItems.size() - 1;
        refreshPlaylist();
    });
}

void FileTrackComponent::addSilenceItem()
{
    playlistItems.push_back(FilePlayerAudioSource::PlaylistItem::createSilence(10.0, 1.0));
    selectedRow = (int)playlistItems.size() - 1;
    refreshPlaylist();
}

void FileTrackComponent::editSelectedItem()
{
    const int selected = selectedRow;
    if (selected < 0 || selected >= (int)playlistItems.size())
        return;

    auto original = playlistItems[(size_t)selected];
    showPlaylistEditor(playlistTable, original,
        original.type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile,
        [this, selected](bool accepted, FilePlayerAudioSource::PlaylistItem updated)
    {
        if (accepted && selected >= 0 && selected < (int)playlistItems.size())
        {
            playlistItems[(size_t)selected] = updated;
            refreshPlaylist();
            playlistTable.selectRow(selected);
        }
    });
}

void FileTrackComponent::removeSelectedItem()
{
    const int selected = selectedRow;
    if (selected < 0 || selected >= (int)playlistItems.size())
        return;

    playlistItems.erase(playlistItems.begin() + selected);
    if (playlistItems.empty())
        selectedRow = -1;
    else
        selectedRow = juce::jlimit(0, (int)playlistItems.size() - 1, selected);
    refreshPlaylist();
}

void FileTrackComponent::refreshPlaylist()
{
    playlistTable.updateContent();
    playlistTable.repaint();
    playlistPlaceholder.setVisible(playlistItems.empty());

    if (playlistItems.empty())
    {
        playlistTable.deselectAllRows();
        selectedRow = -1;
    }
    else
    {
        selectedRow = juce::jlimit(0, (int)playlistItems.size() - 1, selectedRow);
        playlistTable.selectRow(selectedRow, false, true);
    }
    updateButtonStates();

    firstAudioPath.clear();
    for (const auto& item : playlistItems)
    {
        if (item.type == FilePlayerAudioSource::PlaylistItem::ItemType::AudioFile)
        {
            firstAudioPath = item.file.getFullPathName();
            break;
        }
    }

    syncSources();
}

void FileTrackComponent::playlistSelectionChanged(int newSelection)
{
    selectedRow = newSelection;
    if (selectedRow < 0)
        playlistTable.deselectAllRows();
    updateButtonStates();
}

void FileTrackComponent::updateButtonStates()
{
    const bool hasSelection = selectedRow >= 0 && selectedRow < (int)playlistItems.size();
    editButton.setEnabled(hasSelection);
    removeButton.setEnabled(hasSelection);
}

void FileTrackComponent::syncSources()
{
    if (fileSource)
        fileSource->setPlaylist(playlistItems);
    if (streamingFileSource)
        streamingFileSource->setPlaylist(playlistItems);
}

void FileTrackComponent::movePlaylistRowUp(int index)
{
    if (index > 0 && index < (int)playlistItems.size())
    {
        std::swap(playlistItems[(size_t)index], playlistItems[(size_t)index - 1]);
        selectedRow = index - 1;
        refreshPlaylist();
    }
}

void FileTrackComponent::movePlaylistRowDown(int index)
{
    if (index >= 0 && index < (int)playlistItems.size() - 1)
    {
        std::swap(playlistItems[(size_t)index], playlistItems[(size_t)index + 1]);
        selectedRow = index + 1;
        refreshPlaylist();
    }
}

void FileTrackComponent::updatePlaylistDuration(int index, double newDuration)
{
    if (index < 0 || index >= (int)playlistItems.size())
        return;
    playlistItems[(size_t)index].targetDurationSeconds = juce::jmax(0.0, newDuration);
    refreshPlaylist();
}

void FileTrackComponent::updatePlaylistCrossfade(int index, double newCrossfade)
{
    if (index < 0 || index >= (int)playlistItems.size())
        return;
    playlistItems[(size_t)index].crossfadeSeconds = juce::jmax(0.0, newCrossfade);
    refreshPlaylist();
}

void FileTrackComponent::updatePlaylistRepetitions(int index, int newRepetitions)
{
    if (index < 0 || index >= (int)playlistItems.size())
        return;
    playlistItems[(size_t)index].repetitions = newRepetitions;
    refreshPlaylist();
}

FileTrackComponent::SequenceButtonsComponent::SequenceButtonsComponent(FileTrackComponent& ownerIn, int rowIndex)
    : owner(ownerIn), row(rowIndex)
{
    upButton.setClickingTogglesState(false);
    downButton.setClickingTogglesState(false);
    upButton.addListener(this);
    downButton.addListener(this);
    addAndMakeVisible(upButton);
    addAndMakeVisible(downButton);
}

void FileTrackComponent::SequenceButtonsComponent::resized()
{
    auto area = getLocalBounds().reduced(2);
    upButton.setBounds(area.removeFromTop(area.getHeight() / 2).reduced(1));
    downButton.setBounds(area.reduced(1));

    int total = (int)owner.getPlaylistItems().size();
    upButton.setEnabled(total > 0 && row > 0);
    downButton.setEnabled(total > 0 && row < total - 1);
}

void FileTrackComponent::SequenceButtonsComponent::buttonClicked(juce::Button* b)
{
    if (b == &upButton)
        owner.movePlaylistRowUp(row);
    else if (b == &downButton)
        owner.movePlaylistRowDown(row);
}

void FileTrackComponent::SequenceButtonsComponent::updateRow(int newRow)
{
    row = newRow;
    resized();
}

FileTrackComponent::PlaylistValueEditor::PlaylistValueEditor(FileTrackComponent& ownerIn, int rowIndex)
    : owner(ownerIn), row(rowIndex)
{
    setEditable(true, true, false);
    setJustificationType(juce::Justification::centred);
}

FileTrackComponent::DurationValueEditor::DurationValueEditor(FileTrackComponent& ownerIn, int rowIndex)
    : PlaylistValueEditor(ownerIn, rowIndex) {}

void FileTrackComponent::DurationValueEditor::textWasEdited()
{
    double value = getText().getDoubleValue();
    owner.updatePlaylistDuration(row, value > 0.0 ? value : 0.0);
}

FileTrackComponent::CrossfadeValueEditor::CrossfadeValueEditor(FileTrackComponent& ownerIn, int rowIndex)
    : PlaylistValueEditor(ownerIn, rowIndex) {}

void FileTrackComponent::CrossfadeValueEditor::textWasEdited()
{
    double value = juce::jmax(0.0, getText().getDoubleValue());
    owner.updatePlaylistCrossfade(row, value);
}

FileTrackComponent::RepetitionValueEditor::RepetitionValueEditor(FileTrackComponent& ownerIn, int rowIndex)
    : PlaylistValueEditor(ownerIn, rowIndex) {}

void FileTrackComponent::RepetitionValueEditor::textWasEdited()
{
    int value = getText().getIntValue();
    owner.updatePlaylistRepetitions(row, value);
}
