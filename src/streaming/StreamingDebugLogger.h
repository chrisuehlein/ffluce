#pragma once
#include <JuceHeader.h>
#include <chrono>
#include <atomic>

#ifdef JUCE_WINDOWS
#define NOMINMAX  // Prevent Windows min/max macros from conflicting
#include <windows.h>
#endif

/**
 * Comprehensive debug logging for streaming to diagnose crashes
 * Tracks memory usage, buffer states, timing, and system resources
 */
class StreamingDebugLogger
{
public:
    StreamingDebugLogger()
    {
        logFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("FFLUCE_Streaming_Debug_" + 
                                juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".log");
        
        startTime = std::chrono::steady_clock::now();
        logEvent("INIT", "StreamingDebugLogger initialized");
        
        // Log system info
        logSystemInfo();
        
        // Start monitoring thread
        startMonitoring();
    }
    
    ~StreamingDebugLogger()
    {
        stopMonitoring();
        logEvent("SHUTDOWN", "StreamingDebugLogger destroyed");
    }
    
    void logEvent(const juce::String& category, const juce::String& message)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        
        double seconds = elapsed.count() / 1000.0;
        int hours = (int)(seconds / 3600);
        int minutes = (int)((seconds - hours * 3600) / 60);
        double secs = seconds - hours * 3600 - minutes * 60;
        
        juce::String timestamp = juce::String::formatted("[%02d:%02d:%06.3f]", hours, minutes, secs);
        juce::String logLine = timestamp + " [" + category + "] " + message + "\n";
        
        // Thread-safe file writing
        const juce::ScopedLock sl(fileLock);
        logFile.appendText(logLine);
        
        // Also output to console/debugger
        juce::Logger::writeToLog(logLine);
        
        // Check for critical timing (3h 25m = 12300 seconds)
        if (seconds > 12200 && seconds < 12400)
        {
            logLine = "*** APPROACHING CRITICAL TIME WINDOW (3h 25m) ***\n";
            logFile.appendText(logLine);
        }
    }
    
    void logMemoryUsage(const juce::String& context)
    {
        juce::int64 totalMemory = 0;
        juce::int64 freeMemory = 0;
        
        #ifdef JUCE_WINDOWS
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo))
        {
            totalMemory = memInfo.ullTotalPhys;
            freeMemory = memInfo.ullAvailPhys;
        }
        #endif
        
        juce::int64 usedMemory = totalMemory - freeMemory;
        double usedGB = usedMemory / (1024.0 * 1024.0 * 1024.0);
        double freeGB = freeMemory / (1024.0 * 1024.0 * 1024.0);
        
        logEvent("MEMORY", context + " - Used: " + juce::String(usedGB, 2) + "GB, Free: " + juce::String(freeGB, 2) + "GB");
        
        // Warn if memory is getting low
        if (freeGB < 1.0)
        {
            logEvent("WARNING", "Low memory warning! Less than 1GB free");
        }
    }
    
    void logBufferState(const juce::String& bufferName, size_t size, size_t capacity)
    {
        double fillPercent = capacity > 0 ? (100.0 * size / capacity) : 0;
        logEvent("BUFFER", bufferName + " - Size: " + juce::String(size) + 
                          ", Capacity: " + juce::String(capacity) + 
                          ", Fill: " + juce::String(fillPercent, 1) + "%");
    }
    
    void logFFmpegState(bool isRunning, int exitCode = 0)
    {
        if (isRunning)
        {
            ffmpegCrashCount = 0;
            logEvent("FFMPEG", "Process running normally");
        }
        else
        {
            ffmpegCrashCount++;
            logEvent("ERROR", "FFmpeg process stopped! Exit code: " + juce::String(exitCode) + 
                            ", Crash count: " + juce::String(ffmpegCrashCount));
        }
    }
    
    void logFrameStats(int framesProcessed, double fps)
    {
        totalFramesProcessed += framesProcessed;
        logEvent("FRAMES", "Processed: " + juce::String(framesProcessed) + 
                          ", Total: " + juce::String(totalFramesProcessed) + 
                          ", FPS: " + juce::String(fps, 2));
        
        // Check for potential integer overflow at 2^31 frames
        if (totalFramesProcessed > 2000000000)
        {
            logEvent("WARNING", "Approaching frame counter limit!");
        }
    }
    
    void logAudioStats(int samplesWritten, int bufferUnderruns)
    {
        totalSamplesWritten += samplesWritten;
        totalUnderruns += bufferUnderruns;
        
        double hoursOfAudio = totalSamplesWritten / (44100.0 * 2 * 3600); // Stereo 44.1kHz
        
        logEvent("AUDIO", "Samples written: " + juce::String(samplesWritten) + 
                        ", Total hours: " + juce::String(hoursOfAudio, 2) + 
                        ", Underruns: " + juce::String(totalUnderruns));
    }
    
    void logClipTransition(const juce::String& fromClip, const juce::String& toClip, double crossfadeDuration)
    {
        clipTransitionCount++;
        logEvent("TRANSITION", "Clip #" + juce::String(clipTransitionCount) + 
                              ": " + fromClip + " -> " + toClip + 
                              " (crossfade: " + juce::String(crossfadeDuration) + "s)");
    }
    
    void logError(const juce::String& error, const juce::String& context = "")
    {
        errorCount++;
        logEvent("ERROR", "[#" + juce::String(errorCount) + "] " + error + 
                        (context.isNotEmpty() ? " (Context: " + context + ")" : ""));
        
        // Critical error threshold
        if (errorCount > 100)
        {
            logEvent("CRITICAL", "Error count exceeds threshold - system may be unstable");
        }
    }
    
    void logTemperature()
    {
        // Platform-specific temperature monitoring
        #ifdef JUCE_WINDOWS
        // Would need WMI or OpenHardwareMonitor integration
        logEvent("TEMP", "Temperature monitoring not implemented");
        #endif
    }
    
    juce::File getLogFile() const { return logFile; }
    
private:
    void startMonitoring()
    {
        monitoringActive = true;
        monitorThread = std::make_unique<std::thread>([this]()
        {
            while (monitoringActive)
            {
                std::this_thread::sleep_for(std::chrono::minutes(5));
                
                if (monitoringActive)
                {
                    logEvent("HEARTBEAT", "System monitor alive");
                    logMemoryUsage("5-minute check");
                    
                    // Check elapsed time
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
                    
                    if (elapsed.count() > 12000) // Close to 3h 25m
                    {
                        logEvent("WARNING", "Approaching critical time - increasing monitoring frequency");
                        // Could trigger more frequent checks here
                    }
                }
            }
        });
    }
    
    void stopMonitoring()
    {
        monitoringActive = false;
        if (monitorThread && monitorThread->joinable())
        {
            monitorThread->join();
        }
    }
    
    void logSystemInfo()
    {
        logEvent("SYSTEM", "OS: " + juce::SystemStats::getOperatingSystemName());
        logEvent("SYSTEM", "CPU: " + juce::SystemStats::getCpuVendor() + " " + 
                          juce::String(juce::SystemStats::getCpuSpeedInMegahertz()) + "MHz");
        logEvent("SYSTEM", "Cores: " + juce::String(juce::SystemStats::getNumCpus()));
        logEvent("SYSTEM", "RAM: " + juce::String(juce::SystemStats::getMemorySizeInMegabytes()) + "MB");
    }
    
    juce::File logFile;
    juce::CriticalSection fileLock;
    std::chrono::steady_clock::time_point startTime;
    
    // Monitoring thread
    std::unique_ptr<std::thread> monitorThread;
    std::atomic<bool> monitoringActive{false};
    
    // Statistics
    std::atomic<juce::int64> totalFramesProcessed{0};
    std::atomic<juce::int64> totalSamplesWritten{0};
    std::atomic<int> totalUnderruns{0};
    std::atomic<int> clipTransitionCount{0};
    std::atomic<int> errorCount{0};
    std::atomic<int> ffmpegCrashCount{0};
};
