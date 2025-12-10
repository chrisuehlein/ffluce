#include "RenderManager.h"
#include "RenderManagerCore.h"
#include "../audio/BinauralAudioSource.h"
#include "../audio/FilePlayerAudioSource.h"
#include "../audio/NoiseAudioSource.h"

RenderManager::RenderManager(BinauralAudioSource* binauralSource, FilePlayerAudioSource* filePlayer, NoiseAudioSource* noiseSource)
    : core(std::make_unique<RenderManagerCore>(binauralSource, filePlayer, noiseSource))
{
}

RenderManager::~RenderManager()
{
    // Unique_ptr handles destruction
}

bool RenderManager::startRendering(
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
    // Forward to core implementation
    return core->startRendering(
        outputFile,
        introClips,
        loopClips,
        overlayClips,
        totalDuration,
        fadeInDuration,
        fadeOutDuration,
        statusCallback,
        progressCallback,
        useNvidiaAcceleration,
        audioOnly,
        tempNvidiaParams,
        tempCpuParams,
        finalNvidiaParams,
        finalCpuParams);
}

void RenderManager::cancelRendering()
{
    core->cancelRendering();
}

RenderManager::RenderState RenderManager::getState() const
{
    return core->getState();
}

bool RenderManager::isRendering() const
{
    return core->isRendering();
}

juce::String RenderManager::getStatusMessage() const
{
    return core->getStatusMessage();
}