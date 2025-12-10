#pragma once
#include <JuceHeader.h>

/**
 * Process management singleton that keeps track of all running external processes.
 * This ensures that all child processes (like FFmpeg) are properly terminated when the app exits.
 */
class ProcessManager
{
public:
    /** 
     * Gets the singleton instance 
     */
    static ProcessManager& getInstance()
    {
        static ProcessManager instance;
        return instance;
    }

    /**
     * Registers a process with the manager
     * @param process A pointer to the process to register
     * @param description Optional description for logging
     */
    void registerProcess(juce::ChildProcess* process, const juce::String& description = "")
    {
        juce::ScopedLock lock(criticalSection);
        activeProcesses.add(process);
        if (description.isNotEmpty())
            processDescriptions.set(process, description);
        
        juce::Logger::writeToLog("Process registered: " + description);
    }

    /**
     * Unregisters a process when it's being destroyed
     * @param process The process to unregister
     */
    void unregisterProcess(juce::ChildProcess* process)
    {
        juce::ScopedLock lock(criticalSection);
        activeProcesses.removeFirstMatchingValue(process);
        processDescriptions.remove(process);
        
        juce::Logger::writeToLog("Process unregistered");
    }

    /**
     * Terminates all registered processes
     */
    void terminateAllProcesses()
    {
        juce::ScopedLock lock(criticalSection);
        int count = 0;
        
        // Log what we're about to do
        juce::Logger::writeToLog("Terminating all processes (" + juce::String(activeProcesses.size()) + " total)");
        
        // Kill all active processes 
        for (auto* process : activeProcesses)
        {
            if (process != nullptr && process->isRunning())
            {
                juce::String desc = processDescriptions[process];
                juce::Logger::writeToLog("Terminating process: " + desc);
                
                // Kill the process
                process->kill();
                count++;
            }
        }
        
        // Clear the list
        activeProcesses.clear();
        processDescriptions.clear();
        
        juce::Logger::writeToLog("Terminated " + juce::String(count) + " processes");
    }

private:
    ProcessManager() {} // Private constructor for singleton
    
    juce::Array<juce::ChildProcess*> activeProcesses;
    juce::HashMap<juce::ChildProcess*, juce::String> processDescriptions;
    juce::CriticalSection criticalSection;
    
    // Make non-copyable
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
};

/**
 * Wrapper for ChildProcess that automatically registers/unregisters with ProcessManager
 */
class ManagedChildProcess : public juce::ChildProcess
{
public:
    ManagedChildProcess(const juce::String& description = "")
        : description(description)
    {
        ProcessManager::getInstance().registerProcess(this, description);
    }
    
    ~ManagedChildProcess()
    {
        // Make sure the process is killed before unregistering
        if (isRunning())
            kill();
            
        ProcessManager::getInstance().unregisterProcess(this);
    }
    
private:
    juce::String description;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ManagedChildProcess)
};