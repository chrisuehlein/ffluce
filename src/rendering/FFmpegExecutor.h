#pragma once
#include <JuceHeader.h>

//==============================================================================
/**
 * @file FFmpegExecutor.h
 * 
 * This file declares the FFmpegExecutor class which is responsible for running
 * FFmpeg commands as child processes and monitoring their execution.
 * 
 * The class handles:
 * - FFmpeg command execution with process monitoring
 * - Progress tracking and reporting via callbacks
 * - Error handling and logging
 * - Querying file durations via FFprobe
 * - Checking for FFmpeg/NVENC availability
 */

/**
 * ############################################################################
 * # UTILITY CLASS: UTF8String
 * ############################################################################
 * 
 * This utility class properly handles FFmpeg output containing non-ASCII characters
 * by explicitly using UTF-8 encoding when creating JUCE strings.
 * 
 * JUCE's String class will assert when you try to create a string from 8-bit
 * data containing values greater than 127 without specifying the encoding.
 * This class ensures proper UTF-8 handling.
 */
class UTF8String
{
public:
    /**
     * Create a JUCE string from a raw buffer, properly handling non-ASCII chars
     * by using explicit UTF-8 encoding.
     * 
     * @param buffer       The raw character buffer to convert
     * @param bufferLength Length of the buffer, or -1 to calculate using strlen
     * @return             A JUCE String with proper UTF-8 encoding
     */
    static juce::String fromRawBuffer(const char* buffer, int bufferLength = -1)
    {
        if (buffer == nullptr)
            return juce::String();
            
        // If length not provided, calculate it
        if (bufferLength < 0)
            bufferLength = (int)strlen(buffer);
            
        // Create a null-terminated buffer
        std::vector<char> nullTerminated(buffer, buffer + bufferLength);
        nullTerminated.push_back(0); // Ensure null termination
        
        // Create a JUCE string with explicit UTF-8 encoding
        return juce::String(juce::CharPointer_UTF8(nullTerminated.data()));
    }
    
    /**
     * Safely read output from a JUCE ChildProcess and convert it to a String
     * with proper UTF-8 encoding.
     * 
     * @param process The JUCE ChildProcess to read output from
     * @return        A UTF-8 encoded JUCE String
     */
    static juce::String fromProcessOutput(juce::ChildProcess* process)
    {
        if (process == nullptr)
            return juce::String();
        
        // Read raw output directly into a buffer
        char buffer[4096] = {0};
        const int bytesRead = process->readProcessOutput(buffer, sizeof(buffer) - 1);
        
        if (bytesRead <= 0)
            return juce::String();
            
        // Ensure null termination
        buffer[bytesRead] = 0;
        
        // Create JUCE string with explicit UTF-8 encoding
        return juce::String(juce::CharPointer_UTF8(buffer));
    }
    
    /**
     * Read all available output from a JUCE ChildProcess and convert it to a String
     * with proper UTF-8 encoding.
     * 
     * @param process The JUCE ChildProcess to read output from
     * @return        A UTF-8 encoded JUCE String containing all available output
     */
    static juce::String readAllProcessOutput(juce::ChildProcess* process)
    {
        if (process == nullptr)
            return juce::String();
            
        juce::String result;
        char buffer[4096];
        int bytesRead;
        int readAttempts = 0;
        const int maxReadAttempts = 100; // Prevent infinite loops
        
        // Keep reading until no more output is available
        // Add safety checks to prevent access violations
        while (readAttempts < maxReadAttempts)
        {
            try {
                bytesRead = process->readProcessOutput(buffer, sizeof(buffer) - 1);
                if (bytesRead <= 0)
                    break;
                    
                buffer[bytesRead] = 0; // Ensure null termination
                result += juce::String(juce::CharPointer_UTF8(buffer));
                readAttempts++;
            }
            catch (...) {
                // If we get an exception reading process output, break the loop
                break;
            }
        }
        
        return result;
    }
    
    /**
     * Convert a potentially unsafe string to a safe UTF-8 encoded JUCE String.
     * 
     * @param unsafeString The string that might contain non-ASCII characters
     * @return             A properly UTF-8 encoded JUCE String
     */
    static juce::String toSafeString(const juce::String& unsafeString)
    {
        // Convert to UTF-8 bytes, then create a new string with explicit UTF-8 encoding
        const char* utf8Bytes = unsafeString.toUTF8();
        return juce::String(juce::CharPointer_UTF8(utf8Bytes));
    }
};

//==============================================================================
/**
 * The FFmpegExecutor class handles all interaction with FFmpeg as an external process.
 * 
 * This class is responsible for:
 * 1. Running FFmpeg commands via ChildProcess
 * 2. Monitoring progress and reporting via callbacks
 * 3. Parsing FFmpeg output for progress information
 * 4. Detecting file durations using FFprobe
 * 5. Checking for FFmpeg/NVENC availability
 * 
 * @note This class doesn't directly interact with video/audio data - it only
 *       runs FFmpeg commands and reports results. The actual video/audio processing
 *       logic is handled by other classes that use this as a service.
 */
class FFmpegExecutor
{
public:
    struct VideoStreamInfo
    {
        int width = 0;
        int height = 0;
        double fps = 0.0;
    };

    /**
     * Constructor - initializes the executor with default values.
     */
    FFmpegExecutor();
    
    /**
     * Destructor - ensures any running processes are cancelled.
     */
    ~FFmpegExecutor();
    
    /**
     * Sets a callback function that will be called with progress updates.
     * 
     * The callback receives a value between 0.0-1.0 representing the progress percentage.
     * 
     * @param callback Function to be called with progress value
     */
    void setProgressCallback(std::function<void(double)> callback);
    
    /**
     * Sets a callback function that will be called with log messages.
     * 
     * @param logCallback Function to be called with log messages as juce::String
     */
    void setLogCallback(std::function<void(const juce::String&)> logCallback);

    // Override the progress window and estimated duration for the next commands.
    // This lets higher-level coordinators map multiple FFmpeg calls into a single
    // global 0..1 progress bar. Call clearExternalProgressWindow() to stop using it.
    void setExternalProgressWindow(double start, double end, double estimatedDurationSeconds = -1.0);
    void clearExternalProgressWindow();
    
    /**
     * Sets the directory where FFmpeg command output should be recorded.
     * A per-command log file plus an aggregate log will be created in this directory.
     */
    void setSessionLogDirectory(const juce::File& directory);
    
    /**
     * Executes an FFmpeg command as a child process with progress monitoring.
     * 
     * This is the core method that runs FFmpeg and monitors its output for progress.
     * It maps the raw progress of the FFmpeg process (0.0-1.0) to the specified
     * progress range (progressStart-progressEnd).
     * 
     * @param command       The complete FFmpeg command line to execute
     * @param progressStart The starting progress value to report (default: 0.0)
     * @param progressEnd   The ending progress value to report (default: 1.0)
     * @return              true if the command executed successfully, false if it failed or was cancelled
     */
    bool executeCommand(const juce::String& command, double progressStart = 0.0, double progressEnd = 1.0);
    
    /**
     * Executes a command and returns its output as a string.
     *
     * This is useful for querying information via FFmpeg/FFprobe where
     * you need to capture and parse the output.
     *
     * @param command The command line to execute
     * @return        The captured output as a string
     */
    juce::String executeCommandAndGetOutput(const juce::String& command);
    
    /**
     * Cancels the currently running FFmpeg process.
     * 
     * This will set the cancellation flag and kill the process if it's running.
     */
    void cancelExecution();
    
    /**
     * Alternative name for cancelExecution() for API clarity.
     */
    void cancelCurrentCommand() { cancelExecution(); }
    
    /**
     * Gets the path to the FFmpeg executable.
     * 
     * @return Path to FFmpeg, platform-specific (e.g., "ffmpeg" on Unix, "ffmpeg.exe" on Windows)
     */
    juce::String getFFmpegPath();
    
    /**
     * Gets the path to the FFprobe executable.
     * 
     * @return Path to FFprobe, platform-specific
     */
    juce::String getFFprobePath();
    
    /**
     * Checks if FFmpeg is available on the system.
     * 
     * @return true if FFmpeg is available, false otherwise
     */
    bool checkFFmpegAvailability();
    
    /**
     * Checks if NVIDIA hardware encoding (NVENC) is available.
     * 
     * @return true if NVENC is available, false otherwise
     */
    bool isNVENCAvailable();
    
    /**
     * Gets the duration of a video file in seconds using FFprobe.
     * 
     * @param file The video file to check
     * @return     The duration in seconds, or a default value if unavailable
     * 
     * @note #### TEMPORARY SOLUTION ####
     *       The current implementation uses file size to estimate duration
     *       rather than actually calling FFprobe, to avoid JUCE string assertions.
     */
    double getFileDuration(const juce::File& file);
    
    /**
     * Gets the duration of a video file in seconds using FFprobe.
     * Alias for getFileDuration for backward compatibility.
     * 
     * @param file The video file to check
     * @return     The duration in seconds, or -1 if unavailable
     */
    double getVideoDuration(const juce::File& file) { return getFileDuration(file); }

    /**
     * Probes the first video stream of a file and returns width/height/fps.
     *
     * @param file The video file to inspect
     * @return     Populated stream info (fields remain zero if unavailable)
     */
    VideoStreamInfo getVideoStreamInfo(const juce::File& file);
    
    /**
     * Parses an FFmpeg progress line to extract the progress percentage.
     * 
     * @param line The FFmpeg output line to parse
     * @return     A value between 0.0-1.0 representing progress, or -1.0 if no progress info found
     * 
     * @note #### DISABLED ####
     *       This function is currently disabled to avoid JUCE string assertions.
     *       We use a time-based progress approach instead.
     */
    double parseFFmpegProgress(const juce::String& line);
    
    /**
     * Gets the current progress value.
     * 
     * @return A value between 0.0-1.0 representing the current progress
     */
    double getCurrentProgress() const { return currentProgress; }
    
private:
    juce::File getNextCommandLogFile(int& outIndex);
    void writeToAggregateLog(const juce::String& message);
    
    //==========================================================================
    // JUCE-related members
    
    /** The active FFmpeg child process being monitored */
    std::unique_ptr<juce::ChildProcess> activeProcess;
    
    /** Thread-safe mutex for protecting shared state */
    juce::CriticalSection lock;
    
    //==========================================================================
    // Threading and state members
    
    /** Flag to indicate if the current process should be cancelled */
    std::atomic<bool> shouldCancel;
    
    /** Current progress value, atomic for thread safety */
    std::atomic<double> currentProgress {0.0};
    
    //==========================================================================
    // Callback functions
    
    /** Callback function for reporting progress updates */
    std::function<void(double)> progressCallback;
    
    /** Callback function for reporting log messages */
    std::function<void(const juce::String&)> logCallback;
    
    //==========================================================================
    // Logging helpers
    juce::CriticalSection logDirectoryLock;
    juce::File sessionLogDirectory;
    juce::File sessionAggregateLogFile;
    bool sessionLoggingEnabled { false };
    int sessionCommandIndex { 0 };

    // Optional external progress window to map multiple FFmpeg calls onto a single global bar
    bool externalProgressActive;
    double externalProgressStart;
    double externalProgressEnd;
    double externalEstimatedDuration;
    
    // Prevent copying
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegExecutor)
};
