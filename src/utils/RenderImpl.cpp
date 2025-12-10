// Bridging file that forwards to the new modular implementation

#include <JuceHeader.h>
#include "RenderManager.h"
#include "../core/RenderDialog.h"
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"
#include "../core/MainComponent.h"
#include "../rendering/FFmpegExecutor.h"

RenderDialog::RenderDialog(
    BinauralAudioSource* binauralSource,
    FilePlayerAudioSource* filePlayer,
    NoiseAudioSource* noiseSource,
    const std::vector<RenderManager::VideoClipInfo>& introClips,
    const std::vector<RenderManager::VideoClipInfo>& loopClips,
    const std::vector<RenderManager::OverlayClipInfo>& overlayClips,
    double duration,
    double fadeIn,
    double fadeOut)
    : progress(0.0),
      progressBar(progress),
      outputFile()
{
    // Store the parameters
    this->binauralSource = binauralSource;
    this->filePlayer = filePlayer;
    this->noiseSource = noiseSource;
    this->introClips = introClips;
    this->loopClips = loopClips;
    this->overlayClips = overlayClips;
    this->duration = duration;
    this->fadeIn = fadeIn;
    this->fadeOut = fadeOut;

    outputFile = generateDefaultOutputFile(false);
    
    // Setup UI
    setupUI();
}

RenderDialog::~RenderDialog()
{
    stopTimer();
    
    // Cancel rendering if in progress
    if (renderManager && renderManager->isRendering())
        renderManager->cancelRendering();
}

void RenderDialog::setupUI()
{
    // Set size
    setSize(600, 550);
    
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    
    // Info label
    addAndMakeVisible(infoLabel);
    infoLabel.setFont(juce::Font(16.0f));
    infoLabel.setJustificationType(juce::Justification::centred);
    
    // Status and progress
    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(progressBar);
    progressBar.setPercentageDisplay(true);
    
    // File path
    addAndMakeVisible(fileLabel);
    fileLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    
    addAndMakeVisible(filePathEditor);
    filePathEditor.setReadOnly(true);
    filePathEditor.setText(outputFile.getFullPathName(), false);
    
    addAndMakeVisible(browseButton);
    browseButton.onClick = [this] { 
        fileChooser = std::make_unique<juce::FileChooser>("Save Output File", 
                                                         outputFile, 
                                                         audioOnlyToggle.getToggleState() ? "*.wav" : "*.mp4");
        
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, 
            [this](const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile() || fc.getResult().getParentDirectory().isDirectory()) {
                    outputFile = fc.getResult();
                    filePathEditor.setText(outputFile.getFullPathName(), false);
                }
            });
    };
    
    // NVIDIA acceleration
    addAndMakeVisible(useNvidiaAcceleration);
    useNvidiaAcceleration.setToggleState(true, juce::dontSendNotification);
    
    addAndMakeVisible(nvidiaInfoLabel);
    nvidiaInfoLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    nvidiaInfoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    
    // Audio only option
    addAndMakeVisible(audioOnlyToggle);
    audioOnlyToggle.setToggleState(false, juce::dontSendNotification);
    audioOnlyToggle.onClick = [this] { updateFileExtension(); };
    
    addAndMakeVisible(audioOnlyInfoLabel);
    audioOnlyInfoLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    audioOnlyInfoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    
    // Quality settings
    addAndMakeVisible(qualityLabel);
    qualityLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    qualityLabel.setText("Quality:", juce::dontSendNotification);
    
    addAndMakeVisible(qualityCombo);
    qualityCombo.addItem("Highest Quality", static_cast<int>(QualityPreset::Highest) + 1);
    qualityCombo.addItem("Balanced", static_cast<int>(QualityPreset::Balanced) + 1);
    qualityCombo.addItem("Fast Encoding", static_cast<int>(QualityPreset::Fast) + 1);
    qualityCombo.addItem("Draft Quality", static_cast<int>(QualityPreset::Draft) + 1);
    qualityCombo.setSelectedId(static_cast<int>(QualityPreset::Balanced) + 1);
    qualityCombo.onChange = [this] { 
        updateQualityDescription(); 
    };
    
    addAndMakeVisible(qualityDescription);
    qualityDescription.setFont(juce::Font(12.0f, juce::Font::italic));
    qualityDescription.setColour(juce::Label::textColourId, juce::Colours::grey);
    updateQualityDescription();
    
    // Elapsed time
    addAndMakeVisible(elapsedTimeLabel);
    addAndMakeVisible(elapsedTimeValue);
    
    // Buttons
    addAndMakeVisible(renderButton);
    renderButton.addListener(this);
    
    addAndMakeVisible(cancelButton);
    cancelButton.addListener(this);
    
    addAndMakeVisible(closeButton);
    closeButton.addListener(this);
    closeButton.setVisible(false);
}

void RenderDialog::updateFileExtension()
{
    bool audioOnly = audioOnlyToggle.getToggleState();
    
    if (audioOnly && outputFile.getFileExtension().toLowerCase() == ".mp4")
    {
        outputFile = outputFile.withFileExtension(".wav");
        filePathEditor.setText(outputFile.getFullPathName(), false);
    }
    else if (!audioOnly && outputFile.getFileExtension().toLowerCase() == ".wav")
    {
        outputFile = outputFile.withFileExtension(".mp4");
        filePathEditor.setText(outputFile.getFullPathName(), false);
    }
    
    // Update UI elements that depend on audio-only mode
    useNvidiaAcceleration.setEnabled(!audioOnly);
    qualityCombo.setEnabled(!audioOnly);
    qualityLabel.setEnabled(!audioOnly);
    qualityDescription.setEnabled(!audioOnly);
}

void RenderDialog::updateQualityDescription()
{
    QualityPreset preset = static_cast<QualityPreset>(qualityCombo.getSelectedId() - 1);
    
    switch (preset)
    {
        case QualityPreset::Highest:
            qualityDescription.setText("Best quality, slowest encoding", juce::dontSendNotification);
            break;
        
        case QualityPreset::Balanced:
            qualityDescription.setText("Good quality, reasonable speed", juce::dontSendNotification);
            break;
        
        case QualityPreset::Fast:
            qualityDescription.setText("Decent quality, fast encoding", juce::dontSendNotification);
            break;
        
        case QualityPreset::Draft:
            qualityDescription.setText("Lower quality, fastest encoding", juce::dontSendNotification);
            break;
    }
}

juce::String RenderDialog::getEncodingParams(bool useNvidia, bool isFinalEncode) const
{
    QualityPreset preset = static_cast<QualityPreset>(qualityCombo.getSelectedId() - 1);
    
    // Temporary files (use faster settings)
    if (!isFinalEncode)
    {
        if (useNvidia)
            return "-preset lossless -rc constqp -qp 0";  // Lossless NVENC for intermediates
        else
            return "-preset ultrafast -qp 0";  // Lossless CPU intermediates
    }
    
    // Final encode
    const juce::String gopSettings = "-g 60 -keyint_min 60 -pix_fmt yuv420p";

    if (useNvidia)
    {
        switch (preset)
        {
            case QualityPreset::Highest:
                return "-preset p7 -rc vbr_hq -b:v 20M -maxrate 30M -bufsize 60M " + gopSettings;
            case QualityPreset::Balanced:
                return "-preset p5 -rc vbr_hq -b:v 12M -maxrate 18M -bufsize 36M " + gopSettings;
            case QualityPreset::Fast:
                return "-preset p3 -rc vbr -b:v 8M -maxrate 12M -bufsize 24M " + gopSettings;
            case QualityPreset::Draft:
                return "-preset p1 -rc vbr -b:v 5M -maxrate 8M -bufsize 16M " + gopSettings;
        }
    }
    else // CPU encoding
    {
        switch (preset)
        {
            case QualityPreset::Highest:
                return "-preset slow -crf 18 -maxrate 30M -bufsize 60M " + gopSettings;
            case QualityPreset::Balanced:
                return "-preset medium -crf 20 -maxrate 18M -bufsize 36M " + gopSettings;
            case QualityPreset::Fast:
                return "-preset faster -crf 22 -maxrate 12M -bufsize 24M " + gopSettings;
            case QualityPreset::Draft:
                return "-preset ultrafast -crf 28 -maxrate 8M -bufsize 16M " + gopSettings;
        }
    }
    
    return "";  // Fallback
}

void RenderDialog::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RenderDialog::resized()
{
    const int margin = 20;
    const int smallMargin = 10;
    const int labelHeight = 24;
    const int buttonHeight = 30;
    const int buttonWidth = 100;
    
    // Title at top
    titleLabel.setBounds(margin, margin, getWidth() - margin * 2, labelHeight);
    infoLabel.setBounds(margin, titleLabel.getBottom() + smallMargin, getWidth() - margin * 2, labelHeight);
    
    // File selector
    fileLabel.setBounds(margin, infoLabel.getBottom() + margin, 100, labelHeight);
    browseButton.setBounds(getWidth() - margin - 50, fileLabel.getY(), 50, labelHeight);
    filePathEditor.setBounds(fileLabel.getRight() + smallMargin, fileLabel.getY(), 
                           browseButton.getX() - fileLabel.getRight() - smallMargin * 2, labelHeight);
    
    // Quality settings
    qualityLabel.setBounds(margin, fileLabel.getBottom() + margin, 100, labelHeight);
    qualityCombo.setBounds(qualityLabel.getRight() + smallMargin, qualityLabel.getY(), 200, labelHeight);
    qualityDescription.setBounds(qualityCombo.getRight() + smallMargin, qualityCombo.getY(), 
                               getWidth() - qualityCombo.getRight() - margin - smallMargin, labelHeight);
    
    // Options
    useNvidiaAcceleration.setBounds(margin, qualityLabel.getBottom() + margin, getWidth() - margin * 2, labelHeight);
    nvidiaInfoLabel.setBounds(margin + 20, useNvidiaAcceleration.getBottom(), getWidth() - margin * 2 - 20, labelHeight);
    
    audioOnlyToggle.setBounds(margin, nvidiaInfoLabel.getBottom() + margin, getWidth() - margin * 2, labelHeight);
    audioOnlyInfoLabel.setBounds(margin + 20, audioOnlyToggle.getBottom(), getWidth() - margin * 2 - 20, labelHeight);
    
    // Status and progress
    const int bottomSectionY = getHeight() - margin - buttonHeight - margin - labelHeight - margin - labelHeight;
    
    statusLabel.setBounds(margin, bottomSectionY, getWidth() - margin * 2, labelHeight);
    progressBar.setBounds(margin, statusLabel.getBottom() + smallMargin, getWidth() - margin * 2, 20);
    
    // Elapsed time
    elapsedTimeLabel.setBounds(margin, progressBar.getBottom() + margin, 100, labelHeight);
    elapsedTimeValue.setBounds(elapsedTimeLabel.getRight() + smallMargin, elapsedTimeLabel.getY(), 
                             getWidth() - elapsedTimeLabel.getRight() - margin - smallMargin, labelHeight);
    
    // Buttons at bottom
    renderButton.setBounds(getWidth() - margin - buttonWidth, getHeight() - margin - buttonHeight, buttonWidth, buttonHeight);
    cancelButton.setBounds(renderButton.getX() - buttonWidth - smallMargin, renderButton.getY(), buttonWidth, buttonHeight);
    closeButton.setBounds(renderButton.getX(), renderButton.getY(), buttonWidth, buttonHeight);
}

void RenderDialog::buttonClicked(juce::Button* button)
{
    if (button == &renderButton)
    {
        startRender();
    }
    else if (button == &cancelButton)
    {
        if (renderManager && renderManager->isRendering())
        {
            renderManager->cancelRendering();
        }
        else
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }
    else if (button == &closeButton)
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}

void RenderDialog::showDialog(juce::Component* parent)
{
    // Create a dialog window
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(this);
    options.dialogTitle = "Render Project";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    
    options.launchAsync();
}

void RenderDialog::startRender()
{
    // Check inputs
    if ((!audioOnlyToggle.getToggleState() && introClips.empty() && loopClips.empty()) || duration <= 0.0)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                            "Cannot Render",
                                            "Please add at least one video clip and set a valid duration.");
        return;
    }
    
    // Update UI state
    renderButton.setVisible(false);
    closeButton.setVisible(false);
    cancelButton.setEnabled(true);
    cancelButton.setVisible(true);
    filePathEditor.setEnabled(false);
    browseButton.setEnabled(false);
    useNvidiaAcceleration.setEnabled(false);
    audioOnlyToggle.setEnabled(false);
    qualityCombo.setEnabled(false);
    
    // Show progress bar
    progressBar.setVisible(true);
    
    // Set status
    statusLabel.setText("Preparing for rendering...", juce::dontSendNotification);
    
    // Start render time tracking
    renderStartTime = juce::Time::getCurrentTime();
    startTimer(500); // Update elapsed time every 500ms
    
    // Get encoding parameters
    juce::String tempNvidiaParams = getEncodingParams(true, false);
    juce::String tempCpuParams = getEncodingParams(false, false);
    juce::String finalNvidiaParams = getEncodingParams(true, true);
    juce::String finalCpuParams = getEncodingParams(false, true);
    
    // Create a render manager
    renderManager = std::make_unique<RenderManager>(binauralSource, filePlayer, noiseSource);
    
    // Check if NVENC is actually available before enabling it
    bool userWantsNvenc = useNvidiaAcceleration.getToggleState();
    bool nvencAvailable = false;
    
    if (userWantsNvenc) {
        // Create a temporary FFmpegExecutor to check NVENC availability
        auto tempExecutor = std::make_unique<FFmpegExecutor>();
        nvencAvailable = tempExecutor->isNVENCAvailable();
        
        if (!nvencAvailable) {
            juce::Logger::writeToLog("[RENDER] WARNING: NVENC requested but not available, falling back to CPU encoding");
        }
    }
    
    bool finalUseNvenc = userWantsNvenc && nvencAvailable;
    
    // Start rendering
    bool success = renderManager->startRendering(
        outputFile,
        introClips,
        loopClips,
        overlayClips,
        duration,
        fadeIn,
        fadeOut,
        [this](const juce::String& status) { 
            // TEMPORARILY DISABLED: Don't update UI text to prevent string assertions
            // Instead, always show a generic message
            juce::MessageManager::callAsync([this]() {
                statusLabel.setText("Rendering in progress...", juce::dontSendNotification);
            });
            
            // Log the status to file
            juce::Logger::writeToLog("Status from render: " + status);
        },
        [this](double p) { 
            this->progress = p;
            juce::MessageManager::callAsync([this]() {
                progressBar.repaint();
            });
        },
        finalUseNvenc,
        audioOnlyToggle.getToggleState(),
        tempNvidiaParams,
        tempCpuParams,
        finalNvidiaParams,
        finalCpuParams);
    
    if (!success)
    {
        renderFinished(false);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                            "Render Error",
                                            "Failed to start rendering. Please check the console for errors.");
    }
}

void RenderDialog::renderFinished(bool success)
{
    // Update UI
    renderButton.setVisible(false);
    cancelButton.setVisible(false);
    closeButton.setVisible(true);
    filePathEditor.setEnabled(true);
    browseButton.setEnabled(true);
    useNvidiaAcceleration.setEnabled(!audioOnlyToggle.getToggleState());
    audioOnlyToggle.setEnabled(true);
    qualityCombo.setEnabled(!audioOnlyToggle.getToggleState());
    
    // Stop timer
    stopTimer();
    
    // Update status with fixed strings only (to avoid any potential string issues)
    if (success)
    {
        // Set a simple status text to avoid any string issues
        statusLabel.setText("Rendering complete!", juce::dontSendNotification);
        
        // Show success message with file path
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                            "Render Complete",
                                            "Rendering has completed successfully!\n\nOutput file: " + 
                                            outputFile.getFullPathName());
    }
    else
    {
        // Set a simple status text to avoid any string issues
        statusLabel.setText("Rendering cancelled.", juce::dontSendNotification);
    }
}

void RenderDialog::timerCallback()
{
    // Update elapsed time
    juce::RelativeTime elapsed = juce::Time::getCurrentTime() - renderStartTime;
    int hours = static_cast<int>(elapsed.inHours());
    int minutes = static_cast<int>(elapsed.inMinutes()) % 60;
    int seconds = static_cast<int>(elapsed.inSeconds()) % 60;
    
    elapsedTimeValue.setText(juce::String::formatted("%02d:%02d:%02d", hours, minutes, seconds),
                           juce::dontSendNotification);
    
    // Update progress bar
    progressBar.repaint();
    
    // Check if rendering is complete
    if (renderManager && !renderManager->isRendering())
    {
        bool success = renderManager->getState() == RenderManager::RenderState::Completed;
        renderFinished(success);
    }
}

juce::File RenderDialog::generateDefaultOutputFile(bool audioOnly) const
{
    juce::File baseDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                   .getChildFile("FFLUCE Renders");
    if (!baseDirectory.isDirectory())
        baseDirectory.createDirectory();

    juce::StringArray tokens;
    tokens.add(juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"));
    tokens.add(buildRenderDescriptor());

    juce::String baseName = tokens.joinIntoString("_");
    baseName = juce::File::createLegalFileName(baseName);

    const juce::String extension = audioOnly ? ".wav" : ".mp4";
    return baseDirectory.getNonexistentChildFile(baseName, extension, false);
}

juce::String RenderDialog::buildRenderDescriptor() const
{
    juce::StringArray parts;

    if (binauralSource != nullptr)
    {
        juce::String token = "Binaural";
        token += "-L" + formatValueToken(binauralSource->getLeftFrequency(), 1) + "Hz";
        token += "-R" + formatValueToken(binauralSource->getRightFrequency(), 1) + "Hz";
        token += "-G" + formatValueToken(binauralSource->getGain(), 2);
        parts.add(token);
    }
    else
    {
        parts.add("Binaural-NA");
    }

    if (filePlayer != nullptr && filePlayer->isLoaded())
    {
        juce::String trackName = sanitizeToken(filePlayer->getLoadedFile().getFileNameWithoutExtension());
        juce::String token = "Music-" + trackName + "-G" + formatValueToken(filePlayer->getGain(), 2);
        parts.add(token);
    }
    else
    {
        parts.add("Music-Off");
    }

    if (noiseSource != nullptr)
    {
        if (!noiseSource->isMuted() && noiseSource->getGain() > 0.0001f)
        {
            juce::String typeName = "White";
            switch (noiseSource->getNoiseType())
            {
                case NoiseAudioSource::White: typeName = "White"; break;
                case NoiseAudioSource::Pink:  typeName = "Pink";  break;
                case NoiseAudioSource::Brown: typeName = "Brown"; break;
            }

            juce::String token = "Noise-" + typeName + "-G" + formatValueToken(noiseSource->getGain(), 2);
            parts.add(token);
        }
        else
        {
            parts.add("NoiseMuted");
        }
    }

    if (parts.isEmpty())
        parts.add("Render");

    return parts.joinIntoString("_");
}

juce::String RenderDialog::sanitizeToken(const juce::String& token)
{
    juce::String cleaned = token;
    cleaned = cleaned.replaceCharacter(' ', '-');
    cleaned = cleaned.replaceCharacter('\\', '-');
    cleaned = cleaned.replaceCharacter('/', '-');
    cleaned = cleaned.replaceCharacter(':', '-');
    cleaned = cleaned.replaceCharacter(';', '-');
    cleaned = cleaned.replaceCharacter(',', '-');
    cleaned = cleaned.replaceCharacter('"', '-');
    cleaned = cleaned.replaceCharacter('\'', '-');
    cleaned = cleaned.replaceCharacter('|', '-');
    cleaned = cleaned.replaceCharacter('*', '-');
    cleaned = cleaned.replaceCharacter('?', '-');
    cleaned = cleaned.replaceCharacter('<', '-');
    cleaned = cleaned.replaceCharacter('>', '-');
    cleaned = cleaned.replaceCharacter('#', '-');
    cleaned = cleaned.replaceCharacter('%', '-');
    cleaned = cleaned.replaceCharacter('&', '-');
    cleaned = cleaned.replaceCharacter('=', '-');
    cleaned = cleaned.replaceCharacter('+', '-');
    cleaned = cleaned.replaceCharacter('(', '-');
    cleaned = cleaned.replaceCharacter(')', '-');
    cleaned = cleaned.replaceCharacter('[', '-');
    cleaned = cleaned.replaceCharacter(']', '-');
    cleaned = cleaned.replaceCharacter('{', '-');
    cleaned = cleaned.replaceCharacter('}', '-');

    const juce::String allowedCharacters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    juce::String result;
    for (auto c : cleaned)
    {
        if (allowedCharacters.containsChar(c))
            result += c;
    }

    if (result.isEmpty())
        result = "NA";

    constexpr int maxTokenLength = 48;
    if (result.length() > maxTokenLength)
        result = result.substring(0, maxTokenLength);

    return result;
}

juce::String RenderDialog::formatValueToken(double value, int decimals)
{
    juce::String numeric(value, decimals);
    numeric = numeric.replaceCharacter('.', 'p');
    numeric = numeric.replaceCharacter('-', 'n');
    return sanitizeToken(numeric);
}
