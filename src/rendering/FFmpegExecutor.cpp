//==============================================================================
/**
 * @file FFmpegExecutor.cpp
 * 
 * Implementation file for the FFmpegExecutor class, which handles running
 * FFmpeg commands as external processes and monitoring their execution.
 * 
 * The original implementation parsed FFmpeg's output to track progress, but due to
 * JUCE string assertion issues with non-ASCII characters in the output, this has been
 * modified to use a time-based approximation approach instead.
 */

#include "FFmpegExecutor.h"

namespace
{
    double parseFractionString(const juce::String& fraction)
    {
        auto value = fraction.trim();
        if (value.isEmpty())
            return 0.0;

        const int slashIndex = value.indexOfChar('/');
        if (slashIndex > 0)
        {
            const double numerator = value.substring(0, slashIndex).getDoubleValue();
            const double denominator = value.substring(slashIndex + 1).getDoubleValue();
            if (denominator != 0.0)
                return numerator / denominator;
            return 0.0;
        }

        return value.getDoubleValue();
    }
}

//==============================================================================
FFmpegExecutor::FFmpegExecutor()
    : shouldCancel(false),
      currentProgress(0.0),
      externalProgressActive(false),
      externalProgressStart(0.0),
      externalProgressEnd(1.0),
      externalEstimatedDuration(-1.0)
{
    // Initialize the atomic progress counter to 0
    // The shouldCancel flag is also initialized to false
}

//==============================================================================
FFmpegExecutor::~FFmpegExecutor()
{
    // Make sure any running processes are terminated when this object is destroyed
    cancelExecution();
}

//==============================================================================
void FFmpegExecutor::setProgressCallback(std::function<void(double)> callback)
{
    // Store the progress callback function
    progressCallback = callback;
}

//==============================================================================
void FFmpegExecutor::setLogCallback(std::function<void(const juce::String&)> callback)
{
    // Store the log message callback function
    logCallback = callback;
}

void FFmpegExecutor::setExternalProgressWindow(double start, double end, double estimatedDurationSeconds)
{
    externalProgressActive = true;
    externalProgressStart = start;
    externalProgressEnd = end;
    externalEstimatedDuration = estimatedDurationSeconds;
}

void FFmpegExecutor::clearExternalProgressWindow()
{
    externalProgressActive = false;
    externalProgressStart = 0.0;
    externalProgressEnd = 1.0;
    externalEstimatedDuration = -1.0;
}

//==============================================================================
void FFmpegExecutor::setSessionLogDirectory(const juce::File& directory)
{
    juce::ScopedLock sl(logDirectoryLock);
    
    sessionLoggingEnabled = false;
    sessionLogDirectory = juce::File();
    sessionAggregateLogFile = juce::File();
    sessionCommandIndex = 0;
    
    if (!directory.isDirectory())
    {
        juce::File dir = directory;
        if (!dir.exists())
            dir.createDirectory();
        
        if (!dir.isDirectory())
            return;
        
        sessionLogDirectory = dir;
    }
    else
    {
        sessionLogDirectory = directory;
    }
    
    sessionAggregateLogFile = sessionLogDirectory.getChildFile("ffmpeg.log");
    if (sessionAggregateLogFile.existsAsFile())
        sessionAggregateLogFile.deleteFile();
    
    sessionLoggingEnabled = sessionLogDirectory.isDirectory();
}

//==============================================================================
juce::File FFmpegExecutor::getNextCommandLogFile(int& outIndex)
{
    juce::ScopedLock sl(logDirectoryLock);
    
    if (!sessionLoggingEnabled)
    {
        outIndex = -1;
        return juce::File();
    }
    
    ++sessionCommandIndex;
    outIndex = sessionCommandIndex;
    juce::String fileName = juce::String::formatted("ffmpeg_%03d.log", sessionCommandIndex);
    return sessionLogDirectory.getChildFile(fileName);
}

//==============================================================================
void FFmpegExecutor::writeToAggregateLog(const juce::String& message)
{
    juce::ScopedLock sl(logDirectoryLock);
    
    if (!sessionLoggingEnabled)
        return;
    
    juce::FileOutputStream stream(sessionAggregateLogFile, 1024);
    if (stream.openedOk())
        stream.writeText(message + "\n", false, false, nullptr);
}

//==============================================================================
bool FFmpegExecutor::executeCommand(const juce::String& command, double progressStart, double progressEnd)
{
    const juce::Time startTime = juce::Time::getCurrentTime();
    const juce::String startTimeString = startTime.toString(true, true);
    
    int commandLogIndex = -1;
    juce::File commandLogFile = getNextCommandLogFile(commandLogIndex);
    std::unique_ptr<juce::FileOutputStream> commandLogStream;
    juce::String commandIndexLabel = (commandLogIndex > 0)
        ? juce::String::formatted("#%03d", commandLogIndex)
        : juce::String("#---");
    
    if (commandLogFile != juce::File())
    {
        commandLogFile.getParentDirectory().createDirectory();
        auto stream = std::make_unique<juce::FileOutputStream>(commandLogFile);
        if (stream->openedOk())
        {
            stream->writeText("Started: " + startTimeString + "\n", false, false, nullptr);
            stream->writeText("Command: " + command + "\n", false, false, nullptr);
            stream->writeText("------------------------------------------------------------\n", false, false, nullptr);
            stream->flush();
            commandLogStream = std::move(stream);
        }
    }
    
    writeToAggregateLog(commandIndexLabel + " [" + startTimeString + "] START " + command);
    
    activeProcess = std::make_unique<juce::ChildProcess>();
    shouldCancel.store(false);

    if (commandLogStream && commandLogStream->openedOk())
        commandLogStream->writeText("Process starting...\n", false, false, nullptr);

    try {
        if (!activeProcess->start(command))
        {
            if (commandLogStream && commandLogStream->openedOk())
            {
                commandLogStream->writeText("Failed to start FFmpeg process\n", false, false, nullptr);
                commandLogStream->flush();
            }
            writeToAggregateLog(commandIndexLabel + " [" + juce::Time::getCurrentTime().toString(true, true) + "] START_FAILED");
            return false;
        }
    }
    catch (const std::exception& e) {
        if (commandLogStream && commandLogStream->openedOk())
        {
            commandLogStream->writeText("Exception: " + juce::String(e.what()) + "\n", false, false, nullptr);
            commandLogStream->flush();
        }
        writeToAggregateLog(commandIndexLabel + " [" + juce::Time::getCurrentTime().toString(true, true) + "] START_EXCEPTION");
        return false;
    }
    catch (...) {
        if (commandLogStream && commandLogStream->openedOk())
        {
            commandLogStream->writeText("Unknown exception starting process\n", false, false, nullptr);
            commandLogStream->flush();
        }
        writeToAggregateLog(commandIndexLabel + " [" + juce::Time::getCurrentTime().toString(true, true) + "] START_EXCEPTION");
        return false;
    }
    
    // Override progress window if an external window is set
    double effectiveStart = externalProgressActive ? externalProgressStart : progressStart;
    double effectiveEnd = externalProgressActive ? externalProgressEnd : progressEnd;
    double estimatedTotalDuration = 0.0;

    if (externalProgressActive && externalEstimatedDuration > 0.0)
        estimatedTotalDuration = externalEstimatedDuration;

    // Set initial progress value
    currentProgress.store(effectiveStart);
    if (progressCallback)
        progressCallback(effectiveStart);
    
    // Initialize progress tracking
    double lastReportedSeconds = 0.0;
    int progressCheckCounter = 0;
    bool isFirstProgress = true;
    
    // Main process monitoring loop
    try {
        while (activeProcess && activeProcess->isRunning())
        {
            if (shouldCancel.load())
            {
                if (commandLogStream && commandLogStream->openedOk())
                {
                    commandLogStream->writeText("Cancelled\n", false, false, nullptr);
                    commandLogStream->flush();
                }
                writeToAggregateLog(commandIndexLabel + " [" + juce::Time::getCurrentTime().toString(true, true) + "] CANCELLED");
                activeProcess->kill();
                return false;
            }
            
            try {
                // Read output with proper UTF-8 handling but don't display in UI
                char buffer[1024];
                const int bytesRead = activeProcess->readProcessOutput(buffer, sizeof(buffer) - 1);
                
                if (bytesRead > 0)
                {
                    // Process for progress information silently
                    buffer[bytesRead] = 0; // Ensure null termination
                    juce::String output;
                    
                    try {
                        // Try to create a string with explicit UTF-8 encoding
                        output = juce::String(juce::CharPointer_UTF8(buffer));
                        
                        if (commandLogStream && commandLogStream->openedOk())
                        {
                            juce::String sanitizedOutput = output.replace("\r", "\n");
                            commandLogStream->writeText(sanitizedOutput, false, false, nullptr);
                        }
                        
                        // Use time-based progress estimation as fallback
                        juce::StringArray lines;
                        lines.addLines(output);
                        
                        for (const auto& line : lines)
                        {
                            // Look for progress information
                            if (line.contains("time="))
                            {
                                // Extract the time value directly without using a helper method
                                int timePos = line.indexOf("time=");
                                if (timePos >= 0)
                                {
                                    timePos += 5; // Skip "time="
                                    
                                    int endPos = line.indexOfChar(' ', timePos);
                                    if (endPos < 0)
                                        endPos = line.length();
                                        
                                    juce::String timeStr = line.substring(timePos, endPos).trim();
                                    double seconds = 0.0;
                                    
                                    if (timeStr.contains(":"))
                                    {
                                        // Format is HH:MM:SS.ms - parse manually
                                        juce::StringArray parts;
                                        parts.addTokens(timeStr, ":", "");
                                        
                                        if (parts.size() >= 3)
                                        {
                                            seconds = parts[0].getDoubleValue() * 3600.0 +
                                                     parts[1].getDoubleValue() * 60.0 +
                                                     parts[2].getDoubleValue();
                                        }
                                    }
                                    else
                                    {
                                        seconds = timeStr.getDoubleValue();
                                    }
                                    
                                    if (seconds > 0.0)
                                    {
                                        // Use a default estimated duration if we don't have one
                                        if (estimatedTotalDuration <= 0.0)
                                            estimatedTotalDuration = 120.0;
                                            
                                        // Calculate progress
                                        double percentage = juce::jmin(0.95, seconds / estimatedTotalDuration);
                                        double mappedProgress = effectiveStart + (effectiveEnd - effectiveStart) * percentage;
                                        
                                        // Update progress in memory and UI
                                        currentProgress.store(mappedProgress);
                                        
                                        if (progressCallback && (isFirstProgress ||
                                            (seconds - lastReportedSeconds) > 1.0 ||
                                            ++progressCheckCounter % 10 == 0))
                                        {
                                            progressCallback(mappedProgress);
                                            lastReportedSeconds = seconds;
                                            isFirstProgress = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    catch (const std::exception&) { }
                    catch (...) { }
                }
            }
            catch (const std::exception&) { }
            catch (...) { }
            
            // Sleep to avoid consuming too much CPU
            juce::Thread::sleep(100);
        }
    }
    catch (const std::exception&) { }
    catch (...) { }

    if (shouldCancel.load())
    {
        if (commandLogStream && commandLogStream->openedOk())
        {
            commandLogStream->writeText("Cancelled after exit\n", false, false, nullptr);
            commandLogStream->flush();
        }
        writeToAggregateLog(commandIndexLabel + " [" + juce::Time::getCurrentTime().toString(true, true) + "] CANCELLED_AFTER_EXIT");
        activeProcess->kill();
        return false;
    }
    
    int exitCode = 0;

    try {
        if (activeProcess)
            exitCode = activeProcess->getExitCode();
    }
    catch (...) {
        exitCode = -999;
    }

    if (exitCode != 0 && logCallback)
    {
        logCallback("FFmpeg error (exit code: " + juce::String(exitCode) + ")");
        if (exitCode == -28)
            logCallback("No space left on device");
    }
    
    const juce::String finishTimeString = juce::Time::getCurrentTime().toString(true, true);
    if (commandLogStream && commandLogStream->openedOk())
    {
        commandLogStream->writeText("\n------------------------------------------------------------\n", false, false, nullptr);
        commandLogStream->writeText("Finished: " + finishTimeString + "\n", false, false, nullptr);
        commandLogStream->writeText("Exit code: " + juce::String(exitCode) + "\n", false, false, nullptr);
        commandLogStream->flush();
    }
    writeToAggregateLog(commandIndexLabel + " [" + finishTimeString + "] END exitCode=" + juce::String(exitCode));
    
    // Set progress to 100% (end value) upon completion
    currentProgress.store(effectiveEnd);
    if (progressCallback)
        progressCallback(effectiveEnd);
    
    // Return true if the process completed successfully (exit code 0)
    return exitCode == 0;
}

//==============================================================================
void FFmpegExecutor::cancelExecution()
{
    // Set the cancellation flag to signal the monitoring thread
    shouldCancel.store(true);
    
    // If a process is currently running, kill it directly
    if (activeProcess != nullptr && activeProcess->isRunning())
    {
        activeProcess->kill();
    }
}

//==============================================================================
juce::String FFmpegExecutor::executeCommandAndGetOutput(const juce::String& command)
{
    juce::ChildProcess process;

    if (!process.start(command))
        return "";

    process.waitForProcessToFinish(5000);
    return UTF8String::readAllProcessOutput(&process);
}

//==============================================================================
juce::String FFmpegExecutor::getFFmpegPath()
{
    // Use the same approach as VideoPanel - look for FFmpeg next to the executable
    #if JUCE_WINDOWS
    // Get the application's executable directory to look for ffmpeg next to the .exe
    juce::String appPath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName();
    juce::String ffmpegExe = appPath + "\\ffmpeg.exe";
    
    // Check if ffmpeg exists next to our executable
    juce::File ffmpegFile(ffmpegExe);
    if (ffmpegFile.existsAsFile())
    {
        return ffmpegExe;
    }
    
    // Fallback to system PATH
    return "ffmpeg.exe";
    #else
    // For Unix-like systems, check local directory first
    juce::File appDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File localFFmpeg = appDir.getChildFile("ffmpeg");
    if (localFFmpeg.existsAsFile())
    {
        return localFFmpeg.getFullPathName();
    }
    
    // Fallback to system PATH
    return "ffmpeg";
    #endif
}

//==============================================================================
juce::String FFmpegExecutor::getFFprobePath()
{
    // Use the same approach as VideoPanel - look for FFprobe next to the executable
    #if JUCE_WINDOWS
    // Get the application's executable directory to look for ffprobe next to the .exe
    juce::String appPath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName();
    juce::String ffprobeExe = appPath + "\\ffprobe.exe";
    
    // Check if ffprobe exists next to our executable
    juce::File ffprobeFile(ffprobeExe);
    if (ffprobeFile.existsAsFile())
    {
        return ffprobeExe;
    }
    
    // Fallback to system PATH
    return "ffprobe.exe";
    #else
    // For Unix-like systems, check local directory first
    juce::File appDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File localFFprobe = appDir.getChildFile("ffprobe");
    if (localFFprobe.existsAsFile())
    {
        return localFFprobe.getFullPathName();
    }
    
    // Fallback to system PATH
    return "ffprobe";
    #endif
}

//==============================================================================
bool FFmpegExecutor::checkFFmpegAvailability()
{
    // Create a child process to check if FFmpeg is available
    juce::ChildProcess process;
    
    // Run "ffmpeg -version" to check if FFmpeg responds
    if (process.start(getFFmpegPath() + " -version"))
    {
        // Wait for the process to finish (with 2-second timeout)
        process.waitForProcessToFinish(2000);
        
        // If exit code is 0, FFmpeg is available and working
        return process.getExitCode() == 0;
    }
    
    // If the process didn't start, FFmpeg is not available
    return false;
}

//==============================================================================
bool FFmpegExecutor::isNVENCAvailable()
{
    // Create a child process to check for NVENC availability
    juce::ChildProcess process;
    
    // Platform-specific command to check for NVENC encoders
    juce::String command;
    
    #if JUCE_WINDOWS
    // Windows: use findstr instead of grep
    command = "cmd.exe /c \"" + getFFmpegPath() + " -hide_banner -encoders | findstr nvenc\"";
    #else
    // Unix-like: use grep
    command = getFFmpegPath() + " -hide_banner -encoders | grep nvenc";
    #endif
    
    // Execute the command
    if (process.start(command))
    {
        // Wait for the process to finish (with 2-second timeout)
        process.waitForProcessToFinish(2000);
        
        // Read the output using our UTF8String utility for proper handling
        juce::String output = UTF8String::readAllProcessOutput(&process);
        
        // Check if NVENC is mentioned
        return output.contains("nvenc");
    }
    
    // If the process didn't run or no NVENC encoders found, return false
    return false;
}

//==============================================================================
double FFmpegExecutor::getFileDuration(const juce::File& file)
{
    if (!file.existsAsFile())
        return 0.0;

    const juce::String sanitizedPath = file.getFullPathName().replaceCharacter('\\', '/');
    const juce::String command = getFFprobePath() +
        " -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" +
        sanitizedPath + "\"";

    juce::ChildProcess ffprobe;
    if (!ffprobe.start(command))
        return 0.0;

    ffprobe.waitForProcessToFinish(5000);
    juce::String output = UTF8String::readAllProcessOutput(&ffprobe).trim();

    double duration = output.getDoubleValue();
    return (duration > 0.0) ? duration : 0.0;
}

FFmpegExecutor::VideoStreamInfo FFmpegExecutor::getVideoStreamInfo(const juce::File& file)
{
    VideoStreamInfo info;

    if (!file.existsAsFile())
        return info;

    const juce::String sanitizedPath = file.getFullPathName().replaceCharacter('\\', '/');
    const juce::String command = getFFprobePath() +
        " -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate "
        "-of default=noprint_wrappers=1 \"" + sanitizedPath + "\"";

    juce::ChildProcess ffprobe;
    if (!ffprobe.start(command))
        return info;

    ffprobe.waitForProcessToFinish(5000);
    juce::String output = UTF8String::readAllProcessOutput(&ffprobe);

    juce::StringArray lines;
    lines.addLines(output);

    for (auto line : lines)
    {
        juce::String trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        if (trimmed.startsWithIgnoreCase("width="))
        {
            info.width = trimmed.substring(trimmed.indexOfChar('=') + 1).trim().getIntValue();
        }
        else if (trimmed.startsWithIgnoreCase("height="))
        {
            info.height = trimmed.substring(trimmed.indexOfChar('=') + 1).trim().getIntValue();
        }
        else if (trimmed.startsWithIgnoreCase("r_frame_rate="))
        {
            juce::String value = trimmed.substring(trimmed.indexOfChar('=') + 1).trim();
            const double fps = parseFractionString(value);
            if (fps > 0.0)
                info.fps = fps;
        }
    }

    return info;
}

//==============================================================================
double FFmpegExecutor::parseFFmpegProgress(const juce::String& line)
{
    // FFmpeg outputs progress information in lines like:
    // frame=  123 fps= 42 q=29.0 size=    1234kB time=00:00:12.34 bitrate= 123.4kbits/s speed=1.23x
    
    // Look for the time= component which indicates progress
    if (line.contains("time="))
    {
        // Extract the time component
        int timePos = line.indexOf("time=");
        if (timePos != -1)
        {
            timePos += 5; // Skip "time="
            
            // Find the next space after the time
            int endPos = line.indexOfChar(' ', timePos);
            if (endPos == -1)
                endPos = line.length();
                
            // Extract the time string (format usually HH:MM:SS.ms)
            juce::String timeStr = line.substring(timePos, endPos).trim();
            
            // Parse the time format to seconds
            double seconds = 0.0;
            
            // Handle different time formats (00:00:00.00 or 123.45)
            if (timeStr.contains(":"))
            {
                // Format is likely HH:MM:SS.ms
                juce::StringArray timeParts;
                timeParts.addTokens(timeStr, ":", "");
                
                if (timeParts.size() >= 3)
                {
                    // Hours
                    seconds += timeParts[0].getDoubleValue() * 3600.0;
                    // Minutes
                    seconds += timeParts[1].getDoubleValue() * 60.0;
                    // Seconds (may include decimal part)
                    seconds += timeParts[2].getDoubleValue();
                }
            }
            else
            {
                // Format is likely just seconds (123.45)
                seconds = timeStr.getDoubleValue();
            }
            
            if (seconds > 0.0)
                return seconds;
        }
    }
    
    // No progress information found
    return -1.0;
}
