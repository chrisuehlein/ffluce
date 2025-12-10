#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

/**
 * Helper class for loading embedded image resources
 */
class ImageResources
{
public:
    static juce::Image getLogo()
    {
        return juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    }
    
    static juce::Image getPlayIcon()
    {
        return juce::ImageCache::getFromMemory(BinaryData::play_png, BinaryData::play_pngSize);
    }
    
    static juce::Image getStopIcon()
    {
        return juce::ImageCache::getFromMemory(BinaryData::stop_png, BinaryData::stop_pngSize);
    }
    
    // Alternative method using names
    static juce::Image getImageByName(const juce::String& imageName)
    {
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource(imageName.toUTF8(), dataSize);
        
        if (data != nullptr && dataSize > 0)
        {
            return juce::ImageCache::getFromMemory(data, dataSize);
        }
        
        return juce::Image();
    }
};