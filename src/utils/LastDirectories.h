/*
  ==============================================================================
    LastDirectories.h - Remembers last used directories for file dialogs
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

/**
 * Simple singleton to remember the last used directory for each file dialog category.
 * Persists between sessions by saving to a config file.
 */
class LastDirectories
{
public:
    static LastDirectories& getInstance()
    {
        static LastDirectories instance;
        return instance;
    }

    // Directory categories
    enum Category
    {
        Video,          // Intro, loop, overlay videos
        Audio,          // Audio/music files
        Project,        // Project save/load
        RenderOutput    // Render output location
    };

    juce::File getLastDirectory(Category category) const
    {
        auto it = directories.find(category);
        if (it != directories.end() && it->second.isDirectory())
            return it->second;

        // Default to user's documents
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    void setLastDirectory(Category category, const juce::File& file)
    {
        // Store the parent directory if a file was selected, or the directory itself
        juce::File dir = file.isDirectory() ? file : file.getParentDirectory();
        if (dir.isDirectory())
        {
            directories[category] = dir;
            save();
        }
    }

    // Convenience method - call after a file is selected
    void rememberFile(Category category, const juce::File& selectedFile)
    {
        if (selectedFile.exists() || selectedFile.getParentDirectory().isDirectory())
            setLastDirectory(category, selectedFile);
    }

private:
    LastDirectories()
    {
        load();
    }

    std::map<Category, juce::File> directories;

    juce::File getConfigFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("FFLUCE")
                   .getChildFile("last_directories.xml");
    }

    void load()
    {
        auto configFile = getConfigFile();
        if (!configFile.existsAsFile())
            return;

        auto xml = juce::parseXML(configFile);
        if (xml == nullptr)
            return;

        if (xml->hasTagName("LastDirectories"))
        {
            if (xml->hasAttribute("video"))
                directories[Video] = juce::File(xml->getStringAttribute("video"));
            if (xml->hasAttribute("audio"))
                directories[Audio] = juce::File(xml->getStringAttribute("audio"));
            if (xml->hasAttribute("project"))
                directories[Project] = juce::File(xml->getStringAttribute("project"));
            if (xml->hasAttribute("renderOutput"))
                directories[RenderOutput] = juce::File(xml->getStringAttribute("renderOutput"));
        }
    }

    void save()
    {
        auto configFile = getConfigFile();
        configFile.getParentDirectory().createDirectory();

        juce::XmlElement xml("LastDirectories");

        if (directories.count(Video))
            xml.setAttribute("video", directories[Video].getFullPathName());
        if (directories.count(Audio))
            xml.setAttribute("audio", directories[Audio].getFullPathName());
        if (directories.count(Project))
            xml.setAttribute("project", directories[Project].getFullPathName());
        if (directories.count(RenderOutput))
            xml.setAttribute("renderOutput", directories[RenderOutput].getFullPathName());

        xml.writeTo(configFile);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LastDirectories)
};
