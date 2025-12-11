#include "RenderManagerCore.h"

namespace
{
    juce::File findProjectRoot()
    {
        juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        juce::File current = exeDir;
        
        for (int depth = 0; depth < 8; ++depth)
        {
            if (current.getChildFile("Builds").isDirectory())
                return current;
            
            juce::File parent = current.getParentDirectory();
            if (parent == current || !parent.exists())
                break;
            current = parent;
        }
        
        return exeDir;
    }
    
    juce::File ensureLogsRoot()
    {
        juce::File userLogs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("FFLUCE Logs");
        if (userLogs.isDirectory() || userLogs.createDirectory())
            return userLogs;
        
        // Fallback to the legacy Logs folder under the project root if Documents is inaccessible.
        juce::File root = findProjectRoot();
        juce::File fallback = root.getChildFile("Logs");
        fallback.createDirectory();
        return fallback;
    }
    
    juce::File createTimestampedDirectory(const juce::String& prefix)
    {
        juce::File logsRoot = ensureLogsRoot();
        
        juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        juce::File sessionDir = logsRoot.getChildFile(prefix + "_" + timestamp);
        sessionDir.createDirectory();
        return sessionDir;
    }
    
    class SessionLogger : public juce::Logger
    {
    public:
        SessionLogger(const juce::File& destination, juce::Logger* previousLogger)
            : previous(previousLogger), logFile(destination)
        {
            if (logFile.existsAsFile())
                logFile.deleteFile();
            logFile.create();
            
            fileStream = std::make_unique<juce::FileOutputStream>(logFile);
            if (fileStream && fileStream->openedOk())
            {
                const juce::String header = "Console log started at " + juce::Time::getCurrentTime().toString(true, true) + "\n";
                fileStream->writeText(header, false, false, nullptr);
                fileStream->flush();
            }
            else
            {
                fileStream.reset();
            }
        }
        
        ~SessionLogger() override
        {
            juce::ScopedLock lock(writeLock);
            if (fileStream && fileStream->openedOk())
                fileStream->flush();
        }
        
        void logMessage(const juce::String& message) override
        {
            juce::ScopedLock lock(writeLock);
            if (fileStream && fileStream->openedOk())
            {
                const juce::String line = juce::Time::getCurrentTime().toString(true, true) + " | " + message + "\n";
                fileStream->writeText(line, false, false, nullptr);
                fileStream->flush();
            }
            
            if (previous != nullptr)
            {
                juce::Logger* current = juce::Logger::getCurrentLogger();
                
                if (current != previous)
                    juce::Logger::setCurrentLogger(previous);
                
                juce::Logger::writeToLog(message);
                
                if (current != previous)
                    juce::Logger::setCurrentLogger(current);
            }
        }
        
    private:
        juce::Logger* previous = nullptr;
        juce::File logFile;
        std::unique_ptr<juce::FileOutputStream> fileStream;
        juce::CriticalSection writeLock;
    };
}

RenderManagerCore::RenderManagerCore(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer, NoiseAudioSource* noiseSource)
    : Thread("RenderManagerCore"),
      binauralSource(binauralSource),
      filePlayer(filePlayer),
      noiseSource(noiseSource),
      state(RenderState::Idle),
      progress(0.0),
      shouldCancel(false),
      useNvidiaAcceleration(false),
      audioOnly(false)
{
    // Create component instances
    ffmpegExecutor = std::make_unique<FFmpegExecutor>();
    clipProcessor = std::make_unique<ClipProcessor>(ffmpegExecutor.get());
    overlayProcessor = std::make_unique<OverlayProcessor>(ffmpegExecutor.get());
    timelineAssembler = std::make_unique<TimelineAssembler>(ffmpegExecutor.get(), overlayProcessor.get());
    audioRenderer = std::make_unique<AudioRenderer>(binauralSource, filePlayer, noiseSource);
}

RenderManagerCore::~RenderManagerCore()
{
    // Stop any ongoing rendering
    cancelRendering();
    
    // Wait for thread to finish
    if (isThreadRunning())
        stopThread(5000);
    
    // Clean up any temp directories
    cleanup();
}

void RenderManagerCore::initialiseLoggingSession()
{
    teardownLoggingSession();
    
    renderSessionDirectory = createTimestampedDirectory("render");
    
    if (renderSessionDirectory.isDirectory())
    {
        ffmpegLogDirectory = renderSessionDirectory.getChildFile("ffmpeg");
        ffmpegLogDirectory.createDirectory();
        
        if (ffmpegExecutor)
            ffmpegExecutor->setSessionLogDirectory(ffmpegLogDirectory);
        
        juce::File consoleLogFile = renderSessionDirectory.getChildFile("console.log");
        juce::Logger* current = juce::Logger::getCurrentLogger();
        previousLogger = current;
        sessionConsoleLogger = std::make_unique<SessionLogger>(consoleLogFile, current);
        juce::Logger::setCurrentLogger(sessionConsoleLogger.get());
        
        juce::File renderLogFile = renderSessionDirectory.getChildFile("render.log");
        if (renderLogFile.existsAsFile())
            renderLogFile.deleteFile();
        renderLogFile.create();
        
        renderSessionLogStream = std::make_unique<juce::FileOutputStream>(renderLogFile);
        if (renderSessionLogStream && renderSessionLogStream->openedOk())
        {
            renderSessionLogStream->writeText("Render session started at " + juce::Time::getCurrentTime().toString(true, true) + "\n", false, false, nullptr);
            renderSessionLogStream->flush();
        }
        else
        {
            renderSessionLogStream.reset();
        }
    }
    else
    {
        ffmpegLogDirectory = juce::File();
        if (ffmpegExecutor)
            ffmpegExecutor->setSessionLogDirectory(juce::File());
    }
}

void RenderManagerCore::teardownLoggingSession()
{
    {
        juce::ScopedLock lock(logWriteLock);
        if (renderSessionLogStream && renderSessionLogStream->openedOk())
            renderSessionLogStream->flush();
        renderSessionLogStream.reset();
    }
    
    if (ffmpegExecutor)
        ffmpegExecutor->setSessionLogDirectory(juce::File());
    
    if (sessionConsoleLogger)
    {
        juce::Logger::setCurrentLogger(previousLogger);
        sessionConsoleLogger.reset();
    }
    
    previousLogger = nullptr;
    renderSessionDirectory = juce::File();
    ffmpegLogDirectory = juce::File();
}

bool RenderManagerCore::startRendering(
    const juce::File& outputFile,
    const std::vector<VideoClipInfo>& introClips,
    const std::vector<VideoClipInfo>& loopClips,
    const std::vector<OverlayClipInfo>& overlayClips,
    double totalDuration,
    double fadeInDuration,
    double fadeOutDuration,
    std::function<void(const juce::String&)> statusCallback,
    std::function<void(double)> progressCallback,
    bool useNvidiaAcceleration,
    bool audioOnly,
    const juce::String& tempNvidiaParams,
    const juce::String& tempCpuParams,
    const juce::String& finalNvidiaParams,
    const juce::String& finalCpuParams)
{
    // Validate parameters
    if (!outputFile.getParentDirectory().isDirectory())
    {
        juce::String errorMessage = "Output directory doesn't exist: " + outputFile.getParentDirectory().getFullPathName();
        // DISABLED UI TEXT: if (statusCallback) statusCallback(errorMessage);
        juce::Logger::writeToLog("RENDER ERROR: " + errorMessage);
        return false;
    }
    
    if (totalDuration <= 0.0)
    {
        juce::String errorMessage = "Invalid duration: " + juce::String(totalDuration);
        // DISABLED UI TEXT: if (statusCallback) statusCallback(errorMessage);
        juce::Logger::writeToLog("RENDER ERROR: " + errorMessage);
        return false;
    }
    
    // If we're already rendering, cancel it first
    if (isRendering())
        cancelRendering();
    
    // Wait for any existing render thread to finish
    while (isThreadRunning())
        juce::Thread::sleep(100);
    
    // Create working directories
    if (!createWorkingDirectories())
    {
        juce::String errorMessage = "Failed to create working directories";
        // DISABLED UI TEXT: if (statusCallback) statusCallback(errorMessage);
        juce::Logger::writeToLog("RENDER ERROR: " + errorMessage);
        return false;
    }
    
    initialiseLoggingSession();
    
    // Create local log file in temp directory
    logFile = tempDirectory.getChildFile("render_log.txt");
        
    // Set up comprehensive logging function that writes to multiple destinations
    logFunction = [this, statusCallback](const juce::String& message)
    {
        // 1. Write to JUCE logger (which now goes to ambient-render.log)
        juce::Logger::writeToLog("[RENDER] " + message);
        
        const juce::String timestamp = juce::Time::getCurrentTime().toString(true, false);
        const juce::String stampedMessage = timestamp + ": " + message;
        
        {
            juce::ScopedLock lock(logWriteLock);
            if (renderSessionLogStream && renderSessionLogStream->openedOk())
            {
                renderSessionLogStream->writeText(stampedMessage + "\n", false, false, nullptr);
                renderSessionLogStream->flush();
            }
        }
        
        // Also mirror into temp directory log for legacy workflows
        juce::FileOutputStream tempStream(logFile, 1024); // Using 1KB buffer, append mode
        if (tempStream.openedOk()) {
            tempStream.writeText(stampedMessage + "\n", 
                              false, false, nullptr);
        }
        
        // UI text callbacks disabled to prevent string assertions
    };
    
    // Write initial log entries
    logFunction("=== RENDER LOG ===");
    if (renderSessionDirectory.isDirectory())
        logFunction("Log directory: " + renderSessionDirectory.getFullPathName());
    logFunction("Render started at: " + juce::Time::getCurrentTime().toString(true, true));
    logFunction("Output file: " + outputFile.getFullPathName());
    logFunction("Total duration: " + juce::String(totalDuration) + " seconds");
    logFunction("Intro clips: " + juce::String(introClips.size()));
    logFunction("Loop clips: " + juce::String(loopClips.size()));
    logFunction("Overlay clips: " + juce::String(overlayClips.size()));
    logFunction("Using NVIDIA acceleration: " + juce::String(useNvidiaAcceleration ? "yes" : "no"));
    logFunction("Audio only: " + juce::String(audioOnly ? "yes" : "no"));
    
    // Set null log callbacks for all components except progress tracking
    ffmpegExecutor->setLogCallback(logFunction); // Set to our safe function
    clipProcessor->setLogCallback(logFunction);  // Set to our safe function
    timelineAssembler->setLogCallback(logFunction);  // Set to our safe function
    overlayProcessor->setLogCallback(logFunction);  // Set to our safe function
    audioRenderer->setLogCallback(logFunction);  // Set to our safe function
    
    // Store parameters
    this->outputFile = outputFile;
    this->introClips = introClips;
    this->loopClips = loopClips;
    this->overlayClips = overlayClips;
    this->totalDuration = totalDuration;
    this->fadeInDuration = fadeInDuration;
    this->fadeOutDuration = fadeOutDuration;
    this->statusCallback = statusCallback;
    this->progressCallback = progressCallback;

    // Sanitize clip metadata to avoid bogus offsets/durations from earlier UI state or saved projects
    auto sanitizeClip = [](RenderTypes::VideoClipInfo& clip) {
        if (!std::isfinite(clip.startTime) || clip.startTime < 0.0)
            clip.startTime = 0.0;
        if (!std::isfinite(clip.duration) || clip.duration <= 0.0)
            clip.duration = 0.0; // will be clamped later to actual duration
        if (!std::isfinite(clip.crossfade) || clip.crossfade < 0.0)
            clip.crossfade = 0.0;
    };
    for (auto& c : this->introClips) sanitizeClip(c);
    for (auto& c : this->loopClips)  sanitizeClip(c);

    this->useNvidiaAcceleration = useNvidiaAcceleration;
    this->audioOnly = audioOnly;
    this->tempNvidiaParams = tempNvidiaParams;
    this->tempCpuParams = tempCpuParams;
    this->finalNvidiaParams = finalNvidiaParams;
    this->finalCpuParams = finalCpuParams;
    
    // Set encoding parameters for components
    clipProcessor->setEncodingParams(useNvidiaAcceleration, tempNvidiaParams, tempCpuParams);
    timelineAssembler->setEncodingParams(useNvidiaAcceleration, 
                                       tempNvidiaParams, 
                                       tempCpuParams,
                                       finalNvidiaParams,
                                       finalCpuParams);
                                       
    // Log that we're using the new video assembly algorithm
    if (logFunction)
        logFunction("Using the new video assembly algorithm from video_assembly_explained.md");
    
    // Reset progress and state
    progress = 0.0;
    shouldCancel = false;
    renderStartTime = juce::Time::getCurrentTime();
    
    // Update state
    updateState(RenderState::Starting, "Preparing to render...");
    
    // Start render thread
    startThread();
    
    // Start timer to check progress
    startTimer(500);
    
    return true;
}

void RenderManagerCore::cancelRendering()
{
    // Set cancellation flag
    shouldCancel = true;
    
    // Cancel any FFmpeg processes
    ffmpegExecutor->cancelCurrentCommand();
    
    // Stop timer
    stopTimer();
    
    // Update state
    updateState(RenderState::Cancelled, "Rendering cancelled by user");
}

void RenderManagerCore::run()
{
    // Main render thread execution
    bool success = false;
    
    // Create video clips concurrently with audio if not in audio-only mode
    juce::File audioFile;
    std::vector<juce::File> tempVideoFiles;
    
    try
    {
        // Step 1: Render Audio Track
        if (binauralSource != nullptr || filePlayer != nullptr)
        {
            updateState(RenderState::RenderingAudio, "Rendering audio track...");
            
            audioFile = tempDirectory.getChildFile("audio.wav");
            
            // Render combined audio track using the AudioRenderer that was properly initialized
            if (logFunction) {
                if (binauralSource != nullptr && filePlayer != nullptr)
                    logFunction("Rendering combined audio (binaural + file player)...");
                else if (filePlayer != nullptr)
                    logFunction("Rendering audio from file player only...");
                else if (binauralSource != nullptr)
                    logFunction("Rendering binaural audio only...");
            }
            
            // Use the proper AudioRenderer instance with both audio sources
            success = audioRenderer->renderAudio(audioFile, 
                                               totalDuration, 
                                               fadeInDuration,
                                               fadeOutDuration);
            
            // Log detailed information about the audio rendering process
            if (logFunction) {
                logFunction("DETAILED AUDIO RENDERING INFO:");
                logFunction("  - Binaural source available: " + juce::String(binauralSource != nullptr ? "YES" : "NO"));
                logFunction("  - File player available: " + juce::String(filePlayer != nullptr ? "YES" : "NO"));
                logFunction("  - Audio file path: " + audioFile.getFullPathName());
                logFunction("  - Audio file exists: " + juce::String(audioFile.existsAsFile() ? "YES" : "NO"));
                if (audioFile.existsAsFile()) {
                    logFunction("  - Audio file size: " + juce::String(audioFile.getSize() / 1024) + " KB");
                }
                logFunction("  - Rendering success: " + juce::String(success ? "YES" : "NO"));
            }
            
            if (!success)
            {
                if (logFunction)
                    logFunction("Failed to render audio track");
                updateState(RenderState::Failed, "Failed to render audio track");
                return;
            }
            
            if (logFunction)
                logFunction("Audio track rendered successfully: " + audioFile.getFullPathName() + 
                           " (" + juce::String(audioFile.getSize() / 1024 / 1024) + " MB)");
        }
        else
        {
            if (logFunction)
                logFunction("No audio source provided, skipping audio rendering");
        }
        
        // If audio-only mode, produce the audio file and exit
        if (audioOnly)
        {
            updateState(RenderState::Finalizing, "Finalizing audio-only output...");
            
            if (audioFile.existsAsFile())
            {
                // Convert WAV to specified output format if needed
                juce::String outputExtension = outputFile.getFileExtension().toLowerCase();
                
                if (outputExtension == ".wav")
                {
                    // Direct copy
                    success = audioFile.copyFileTo(outputFile);
                }
                else
                {
                    // Convert to target format
                    juce::String audioCommand = 
                        ffmpegExecutor->getFFmpegPath() + 
                        " -y -i \"" + audioFile.getFullPathName() + "\"" +
                        " -vn -c:a " + (outputExtension == ".mp3" ? "libmp3lame -q:a 2" : "aac -b:a 320k") +
                        " \"" + outputFile.getFullPathName() + "\"";
                    
                    success = ffmpegExecutor->executeCommand(audioCommand, 0.8, 1.0);
                }
                
                if (success)
                {
                    if (logFunction)
                        logFunction("Audio-only output created successfully: " + outputFile.getFullPathName());
                    updateState(RenderState::Completed, "Audio-only rendering completed in " + getElapsedTimeString());
                }
                else
                {
                    if (logFunction)
                        logFunction("Failed to create audio-only output");
                    updateState(RenderState::Failed, "Failed to create audio-only output");
                }
            }
            else
            {
                if (logFunction)
                    logFunction("No audio file was created");
                updateState(RenderState::Failed, "No audio file was created");
            }
            
            return;
        }
        
        // Step 2: Process Video Clips
        if (!shouldCancel)
        {
            updateState(RenderState::ProcessingClips, "Processing video clips...");
            
            // Process intro clips
            if (!introClips.empty())
            {
                if (logFunction)
                    logFunction("Processing " + juce::String(introClips.size()) + " intro clips");
                
                for (size_t i = 0; i < introClips.size(); i++)
                {
                    if (shouldCancel) break;
                    
                    const auto& clip = introClips[i];
                    juce::File outputFile = tempDirectory.getChildFile("intro_clip_" + juce::String(i) + ".mp4");
                    
                    if (logFunction)
                        logFunction("Processing intro clip " + juce::String(i) + ": " + clip.file.getFileName());
                    
                    // Trim and process the clip
                    success = clipProcessor->processClip(clip.file, 
                                                       clip.startTime, 
                                                       clip.duration,
                                                       outputFile);
                    
                    if (success)
                    {
                        tempVideoFiles.push_back(outputFile);
                        
                        if (logFunction)
                            logFunction("Intro clip " + juce::String(i) + " processed successfully: " + 
                                      outputFile.getFileName() + " (" + juce::String(outputFile.getSize() / 1024) + " KB)");
                    }
                    else
                    {
                        if (logFunction)
                            logFunction("Failed to process intro clip " + juce::String(i));
                    }
                }
            }
            
            // Process loop clips
            if (!loopClips.empty())
            {
                if (logFunction)
                    logFunction("Processing " + juce::String(loopClips.size()) + " loop clips");
                
                for (size_t i = 0; i < loopClips.size(); i++)
                {
                    if (shouldCancel) break;
                    
                    const auto& clip = loopClips[i];
                    juce::File outputFile = tempDirectory.getChildFile("loop_clip_" + juce::String(i) + ".mp4");
                    
                    if (logFunction)
                        logFunction("Processing loop clip " + juce::String(i) + ": " + clip.file.getFileName());
                    
                    // Trim and process the clip
                    success = clipProcessor->processClip(clip.file, 
                                                       clip.startTime, 
                                                       clip.duration,
                                                       outputFile);
                    
                    if (success)
                    {
                        tempVideoFiles.push_back(outputFile);
                        
                        if (logFunction)
                            logFunction("Loop clip " + juce::String(i) + " processed successfully: " + 
                                      outputFile.getFileName() + " (" + juce::String(outputFile.getSize() / 1024) + " KB)");
                    }
                    else
                    {
                        if (logFunction)
                            logFunction("Failed to process loop clip " + juce::String(i));
                    }
                }
            }
        }
    }
    catch (std::exception& e)
    {
        if (logFunction)
            logFunction("Exception during clip processing: " + juce::String(e.what()));
    }
    
    // Step 3: Render Crossfades
    if (success && !shouldCancel) {
        updateState(RenderState::RenderingCrossfades, "Rendering crossfades between clips...");
        
        if (!audioOnly && tempVideoFiles.size() > 0) {
            if (introClips.size() > 0) {
                for (size_t i = 0; i < introClips.size() - 1; i++) {
                    if (shouldCancel) break;
                    
                    // Safety check for index
                    if (i >= tempVideoFiles.size() || i + 1 >= tempVideoFiles.size()) {
                        if (logFunction)
                            logFunction("ERROR: Invalid index for intro clip crossfade: " + juce::String(i));
                        continue; // Skip this crossfade but don't fail
                    }
                    
                    juce::File fromClip = tempVideoFiles[i];
                    juce::File toClip = tempVideoFiles[i + 1];
                    double crossfadeDuration = introClips[i].crossfade;
                    double fromClipDuration = introClips[i].duration;  // Use EXACT conformed duration
                    double toClipDuration = introClips[i + 1].duration;  // Use EXACT conformed duration
                    
                    if (crossfadeDuration <= 0.001)
                        continue;

                    success = true;
                }
            }
            
            if (loopClips.size() > 0) {
                for (size_t i = 0; i < loopClips.size() - 1; i++) {
                    if (shouldCancel) break;

                    size_t fromIndex = introClips.size() + i;
                    size_t toIndex = introClips.size() + i + 1;

                    if (fromIndex >= tempVideoFiles.size() || toIndex >= tempVideoFiles.size())
                        continue;

                    double crossfadeDuration = loopClips[i].crossfade;

                    if (crossfadeDuration <= 0.001)
                        continue;

                    success = true;
                }

                // Create loop_from_loop_sequence_x crossfade (last loop to first loop)
                if (loopClips.size() > 0 && !shouldCancel) {
                    size_t lastLoopIndex = introClips.size() + loopClips.size() - 1;
                    size_t firstLoopIndex = introClips.size();

                    if (lastLoopIndex < tempVideoFiles.size() && firstLoopIndex < tempVideoFiles.size()) {
                        double crossfadeDuration = loopClips.back().crossfade;
                        if (crossfadeDuration > 0.001)
                            success = true;
                    }
                }
                
                // Create intro-to-loop crossfade
                {
                    juce::File lastIntroClip;
                    juce::File firstLoopClip;

                    if (introClips.size() > 0 && introClips.size() - 1 < tempVideoFiles.size() &&
                        introClips.size() < tempVideoFiles.size()) {
                        lastIntroClip = tempVideoFiles[introClips.size() - 1];
                        firstLoopClip = tempVideoFiles[introClips.size()];
                    }
                    else if (introClips.size() > 0 && loopClips.size() > 0) {
                        if (introClips.size() - 1 < tempVideoFiles.size())
                            lastIntroClip = tempVideoFiles[introClips.size() - 1];
                        else if (tempVideoFiles.size() > 0)
                            lastIntroClip = tempVideoFiles[0];

                        if (introClips.size() < tempVideoFiles.size())
                            firstLoopClip = tempVideoFiles[introClips.size()];
                        else if (tempVideoFiles.size() > 1)
                            firstLoopClip = tempVideoFiles[1];
                        else if (tempVideoFiles.size() > 0)
                            firstLoopClip = tempVideoFiles[0];

                        if (!lastIntroClip.existsAsFile() || !firstLoopClip.existsAsFile())
                            success = false;
                    }

                    double crossfadeDuration = 0.0;
                    if (!introClips.empty())
                        crossfadeDuration = introClips.back().crossfade;

                    bool skipCrossfade = (crossfadeDuration <= 0.001);
                    
                    if (!skipCrossfade)
                        success = true;
                }
            }
        }
    }
    
    // Step 4: Assemble Timeline
    if (!shouldCancel)
    {
        updateState(RenderState::AssemblingTimeline, "Assembling final timeline...");

        success = timelineAssembler->assembleTimeline(introClips,
                                                     loopClips,
                                                     overlayClips,
                                                     audioFile,
                                                     totalDuration,
                                                     tempDirectory,
                                                     outputFile,
                                                     fadeInDuration,
                                                     fadeOutDuration);

        if (!success)
        {
            updateState(RenderState::Failed, "Failed to assemble timeline");
            return;
        }
    }
    
    // Finalize
    if (shouldCancel)
    {
        updateState(RenderState::Cancelled, "Rendering cancelled by user");
    }
    else if (success)
    {
        renderEndTime = juce::Time::getCurrentTime();
        updateState(RenderState::Completed, "Rendering completed in " + getElapsedTimeString());
    }
    else
    {
        updateState(RenderState::Failed, "Rendering failed");
    }
}

void RenderManagerCore::timerCallback()
{
    try {
        // Check progress of FFmpeg process
        double currentProgress = ffmpegExecutor->getCurrentProgress();
        
        if (currentProgress > 0.0)
        {
            progress = currentProgress;
            
            if (progressCallback)
                progressCallback(progress);
        }
    }
    catch (const std::exception& e) {
        // Log any exceptions that might occur during progress checking
        if (logFunction)
            logFunction("Exception in progress update: " + juce::String(e.what()));
    }
    catch (...) {
        // Catch any other exceptions
        if (logFunction)
            logFunction("Unknown exception in progress update");
    }
}

void RenderManagerCore::updateState(RenderState newState, const juce::String& statusMessage)
{
    // Update state
    state = newState;
    currentStatusMessage = statusMessage;
    
    // TEMPORARILY DISABLED: Call status callback
    // Disable text UI updates during rendering to prevent string assertions
    // if (statusCallback)
    //     statusCallback(statusMessage);
    
    // Log to file system logger instead
    juce::Logger::writeToLog("RENDER STATUS: " + statusMessage);
    
    // Log state change
    if (logFunction)
        logFunction("State changed to: " + juce::String(static_cast<int>(newState)) + " - " + statusMessage);
}

bool RenderManagerCore::createWorkingDirectories()
{
    // Create temporary directory in the build folder instead of AppData/Local/Temp
    juce::File projectRoot = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                               .getParentDirectory(); // this should be the build dir
    juce::File tempBaseDir = projectRoot.getChildFile("AmbientRender_Debug");
    
    // DRASTIC CHANGE: Force clean all previous render directories
    if (tempBaseDir.exists()) {
        if (logFunction)
            logFunction("!!! EMERGENCY !!! Forcibly cleaning all previous render directories");
            
        // First try to delete the entire temp directory
        bool deleteSuccess = tempBaseDir.deleteRecursively();
        
        if (!deleteSuccess) {
            if (logFunction)
                logFunction("!!! WARNING !!! Failed to delete temp directory - will try to create a new unique folder");
        }
    }
    
    // Create a unique subdirectory for this render
    juce::String uniqueId = juce::String(juce::Random::getSystemRandom().nextInt()) + 
                         juce::String(juce::Time::currentTimeMillis());
    tempDirectory = tempBaseDir.getChildFile(uniqueId);
    
    // Create directories (creating parent directory if it doesn't exist)
    tempBaseDir.createDirectory();
    bool success = tempDirectory.createDirectory();
    
    if (!success)
        return false;
    
    // Create a debug_info.txt file directly in the temp directory for easy access
    juce::File debugInfoFile = tempDirectory.getChildFile("DEBUG_INFO.txt");
    juce::FileOutputStream debugStream(debugInfoFile);
    if (debugStream.openedOk()) {
        debugStream.writeText("=== DEBUG DIRECTORY INFO ===\n", false, false, nullptr);
        debugStream.writeText("Debug directory: " + tempDirectory.getFullPathName() + "\n", false, false, nullptr);
        debugStream.writeText("Created at: " + juce::Time::getCurrentTime().toString(true, true) + "\n", false, false, nullptr);
        debugStream.writeText("EMERGENCY MODE: All previous render files deleted\n", false, false, nullptr);
    }
    
    return true;
}

void RenderManagerCore::cleanup()
{
    // DO NOT clean up temp directory - we want to keep debug files
    if (tempDirectory.exists())
    {
        // Create a marker file to indicate render is complete
        juce::File markerFile = tempDirectory.getChildFile("RENDER_COMPLETE.txt");
        juce::FileOutputStream markerStream(markerFile);
        if (markerStream.openedOk()) {
            markerStream.writeText("Render completed at: " + juce::Time::getCurrentTime().toString(true, true) + "\n", false, false, nullptr);
        }
        
        // Log the location of debug files
        if (logFunction) {
            logFunction("=== DEBUG FILES LOCATION ===");
            logFunction("Debug directory: " + tempDirectory.getFullPathName());
            logFunction("This directory contains all temporary files used during rendering");
            logFunction("==============================");
        }
        // DO NOT delete the temp directory - we need it for debugging
    }
    
    teardownLoggingSession();
}

double RenderManagerCore::getElapsedRenderTimeSeconds() const
{
    return (renderEndTime - renderStartTime).inSeconds();
}

juce::String RenderManagerCore::getElapsedTimeString() const
{
    int seconds = static_cast<int>(getElapsedRenderTimeSeconds());
    
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    seconds %= 60;
    
    juce::String result;
    
    if (hours > 0)
        result += juce::String(hours) + "h ";
    
    if (minutes > 0 || hours > 0)
        result += juce::String(minutes) + "m ";
    
    result += juce::String(seconds) + "s";
    
    return result;
}
