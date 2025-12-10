/*
  ==============================================================================
    Main.cpp - Application Entry Point
  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"
#include "ProcessManager.h"

class FFLUCEApplication : public juce::JUCEApplication
{
public:
    FFLUCEApplication() {}

    const juce::String getApplicationName() override       { return "FFLUCE"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        // Set up file logging
        juce::File logsDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                        .getChildFile("FFLUCE Logs");
        if (!logsDirectory.isDirectory() && !logsDirectory.createDirectory())
            logsDirectory = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                .getParentDirectory();

        juce::String sessionStamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        juce::File logFile = logsDirectory.getChildFile("FFLUCE_" + sessionStamp + ".log");

        fileLogger.reset(new juce::FileLogger(logFile, "FFLUCE Session Log", 0));
        juce::Logger::setCurrentLogger(fileLogger.get());

        juce::Logger::writeToLog("----------------------------------------------------");
        juce::Logger::writeToLog("Application started: " + juce::Time::getCurrentTime().toString(true, true));
        juce::Logger::writeToLog("Version: " + getApplicationVersion());
        juce::Logger::writeToLog("----------------------------------------------------");

        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        juce::Logger::writeToLog("----------------------------------------------------");
        juce::Logger::writeToLog("Application shutting down: " + juce::Time::getCurrentTime().toString(true, true));
        juce::Logger::writeToLog("----------------------------------------------------");

        ProcessManager::getInstance().terminateAllProcesses();
        mainWindow = nullptr;

        juce::Logger::setCurrentLogger(nullptr);
        fileLogger = nullptr;
    }

    void systemRequestedQuit() override
    {
        ProcessManager::getInstance().terminateAllProcesses();
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override
    {
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
           #else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
           #endif

            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> fileLogger;
};

START_JUCE_APPLICATION(FFLUCEApplication)
