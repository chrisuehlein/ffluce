#include "ClipProcessor.h"

namespace
{
    juce::String buildEncodingParams(const juce::String& baseParams, bool useNvenc)
    {
        juce::StringArray tokens;
        tokens.addTokens(baseParams, " ", "\"'");
        tokens.trim();
        tokens.removeEmptyStrings();
        
        juce::StringArray filtered;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const juce::String& token = tokens[i];
            
            if (token == "-c:v")
            {
                ++i; // Skip the codec value token
                continue;
            }
            
            if (token.startsWith("-c:v"))
            {
                // Handle cases like "-c:v=h264_nvenc" or similar (unlikely but safe)
                continue;
            }
            
            if (token == "-an")
                continue; // We add -an explicitly later
            
            auto skipOptionWithValue = [&](const juce::String& opt)
            {
                if (token == opt || token.startsWith(opt + "="))
                {
                    if (token == opt && i + 1 < tokens.size())
                        ++i; // Skip the value token as well
                    return true;
                }
                return false;
            };
            
            if (skipOptionWithValue("-pix_fmt") ||
                skipOptionWithValue("-profile:v") ||
                skipOptionWithValue("-level") ||
                skipOptionWithValue("-movflags"))
            {
                continue;
            }
            
            filtered.add(token);
        }
        
        juce::String result = "-c:v ";
        result += useNvenc ? "h264_nvenc" : "libx264";
        
        const juce::String extras = filtered.joinIntoString(" ");
        if (extras.isNotEmpty())
        {
            result += " ";
            result += extras;
        }
        
        return result.trim();
    }
}

ClipProcessor::ClipProcessor(FFmpegExecutor* ffmpegExecutor)
    : ffmpegExecutor(ffmpegExecutor),
      useNvidiaAcceleration(false)
{
}

ClipProcessor::~ClipProcessor()
{
}

void ClipProcessor::setLogCallback(std::function<void(const juce::String&)> callback)
{
    logCallback = callback;
}

void ClipProcessor::setEncodingParams(bool useNvidiaAcceleration, 
                                    const juce::String& tempNvidiaParams,
                                    const juce::String& tempCpuParams)
{
    this->useNvidiaAcceleration = useNvidiaAcceleration;
    static const juce::String defaultTempNv = "-preset lossless -rc constqp -qp 0";
    static const juce::String defaultTempCpu = "-preset ultrafast -qp 0";

    juce::String nvParams = tempNvidiaParams.trim().isEmpty() ? defaultTempNv : tempNvidiaParams;
    juce::String cpuParams = tempCpuParams.trim().isEmpty() ? defaultTempCpu : tempCpuParams;

    this->tempNvidiaParams = buildEncodingParams(nvParams, true);
    this->tempCpuParams = buildEncodingParams(cpuParams, false);
}

bool ClipProcessor::prepareVideoClips(const std::vector<RenderTypes::VideoClipInfo>& sourceClips,
                                    const juce::File& tempDirectory,
                                    std::vector<juce::File>& tempVideoFiles)
{
    if (logCallback)
        logCallback("Preparing " + juce::String(sourceClips.size()) + " video clips");
    
    // Clear output vector
    tempVideoFiles.clear();
    
    // Process each clip
    for (size_t i = 0; i < sourceClips.size(); ++i)
    {
        const auto& clipInfo = sourceClips[i];
        
        if (logCallback)
            logCallback("Processing clip " + juce::String(i + 1) + "/" + juce::String(sourceClips.size()) + 
                       ": " + clipInfo.file.getFileName());
                       
        // Verify the clip file exists and is valid
        if (!clipInfo.file.existsAsFile()) {
            if (logCallback)
                logCallback("ERROR: Clip file does not exist: " + clipInfo.file.getFullPathName());
            continue; // Skip this clip instead of failing the entire process
        }
        
        // Create output file name
        juce::String prefix = clipInfo.isIntroClip ? "intro" : "loop";
        juce::File outputFile = tempDirectory.getChildFile(prefix + "_clip_" + juce::String(i) + ".mp4");
        
        // Build FFmpeg command
        juce::String encodingParams = buildEncodingParams(useNvidiaAcceleration ? tempNvidiaParams : tempCpuParams,
                                                         useNvidiaAcceleration);
        
        // Validate duration before building command
        if (clipInfo.duration <= 0.001) {
            if (logCallback)
                logCallback("ERROR: Invalid duration for clip: " + juce::String(clipInfo.duration));
            continue; // Skip this clip
        }
        
        if (logCallback)
            logCallback("Processing clip with duration: " + juce::String(clipInfo.duration, 3) + " seconds");
            
        juce::String command = ffmpegExecutor->getFFmpegPath();
        command += " -y";                                // Overwrite output
        command += " -i \"" + clipInfo.file.getFullPathName() + "\"";
        command += " -t " + juce::String(clipInfo.duration, 3); // Exact duration with fixed precision
        command += " ";
        command += encodingParams;
        command += " -pix_fmt yuv420p";                  // Ensure compatibility with players
        command += " -an";                               // No audio for clips
        command += " -movflags +faststart";              // Optimize for web streaming
        command += " \"" + outputFile.getFullPathName() + "\"";
        
        // Execute the command
        bool commandSucceeded = false;
        
        if (!ffmpegExecutor->executeCommand(command, 0.0, 1.0))
        {
            if (logCallback)
                logCallback("ERROR: Failed to process clip " + clipInfo.file.getFileName());
            
            // If NVENC failed, try with CPU as fallback
            if (useNvidiaAcceleration)
            {
                if (logCallback)
                    logCallback("  NVENC failed, trying CPU encoding as fallback");
                
                juce::String cpuEncoding = buildEncodingParams(tempCpuParams, false);
                
                command = ffmpegExecutor->getFFmpegPath();
                command += " -y";                                // Overwrite output
                command += " -i \"" + clipInfo.file.getFullPathName() + "\"";
                command += " -t " + juce::String(clipInfo.duration, 3); // Exact duration with fixed precision
                command += " ";
                command += cpuEncoding;
                command += " -pix_fmt yuv420p";                   // Ensure compatibility with players
                command += " -an";                                // No audio for clips
                command += " -movflags +faststart";               // Optimize for web streaming
                command += " \"" + outputFile.getFullPathName() + "\"";
                    
                if (logCallback)
                    logCallback("  Trying CPU fallback with duration: " + juce::String(clipInfo.duration, 3));
                
                commandSucceeded = ffmpegExecutor->executeCommand(command, 0.0, 1.0);
                
                if (!commandSucceeded && logCallback)
                    logCallback("ERROR: CPU fallback also failed for clip " + clipInfo.file.getFileName());
            }
        }
        else
        {
            commandSucceeded = true;
        }
        
        // Verify the output file was created correctly
        if (commandSucceeded && outputFile.existsAsFile() && outputFile.getSize() > 0)
        {
            // Add to output vector
            tempVideoFiles.push_back(outputFile);
            
            if (logCallback)
                logCallback("Successfully processed clip: " + clipInfo.file.getFileName());
        }
        else if (logCallback)
        {
            logCallback("WARNING: Failed to create valid output file for clip: " + clipInfo.file.getFileName());
        }
    }
    
    // Check if we processed at least one clip
    if (tempVideoFiles.empty())
    {
        if (logCallback)
            logCallback("ERROR: Failed to process any clips successfully");
        return false;
    }
    
    return true;
}

bool ClipProcessor::splitClipsForCrossfades(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                          const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                          const std::vector<juce::File>& tempVideoFiles,
                                          const juce::File& tempDirectory,
                                          std::vector<juce::File>& tempSplitFiles)
{
    // Implementation would go here
    // This is a placeholder implementation
    return true;
}

double ClipProcessor::getClipDuration(const juce::File& clip)
{
    return ffmpegExecutor->getFileDuration(clip);
}

bool ClipProcessor::processClip(const juce::File& sourceClip,
                               double startTime,
                               double duration,
                               const juce::File& outputFile)
{
    if (logCallback)
        logCallback("Processing clip: " + sourceClip.getFileName());
        
    // Verify the source clip exists
    if (!sourceClip.existsAsFile()) {
        if (logCallback)
            logCallback("ERROR: Source clip does not exist: " + sourceClip.getFullPathName());
        return false;
    }
    
    // Build FFmpeg command
    juce::String encodingParams = buildEncodingParams(useNvidiaAcceleration ? tempNvidiaParams : tempCpuParams,
                                                     useNvidiaAcceleration);
    
    juce::String command = ffmpegExecutor->getFFmpegPath();
    command += " -y";                               // Overwrite output
    command += " -i \"" + sourceClip.getFullPathName() + "\"";
        
    // Add seek offset if needed but ensure it's a valid value
    // Validate the startTime to ensure it's not an invalid floating point value
    if (startTime > 0.0001) { // Only use if significantly greater than zero
        // Format with fixed precision to avoid scientific notation
        command += " -ss " + juce::String(startTime, 3);
        if (logCallback)
            logCallback("  Using start time: " + juce::String(startTime, 3) + " seconds");
    } else {
        // Explicitly set to 0 if it's too small or invalid
        startTime = 0.0;
        if (logCallback)
            logCallback("  Start time too small or invalid, using 0.0 seconds");
    }
        
    // Add duration limit with validation
    if (duration > 0.001) { // Only use if reasonably greater than zero
        // Format with fixed precision to avoid scientific notation
        command += " -t " + juce::String(duration, 3);
        if (logCallback)
            logCallback("  Using duration: " + juce::String(duration, 3) + " seconds");
    } else {
        if (logCallback)
            logCallback("  WARNING: Invalid duration value: " + juce::String(duration));
        return false; // Don't proceed with invalid duration
    }
        
    // Add encoding parameters
    command += " ";
    command += encodingParams;
    command += " -pix_fmt yuv420p"; // Ensure compatibility with players
    command += " -an"; // No audio for clips
    command += " -movflags +faststart"; // Optimize for web streaming
    command += " \"" + outputFile.getFullPathName() + "\"";
    
    // Execute the command
    bool commandSucceeded = false;
    
    if (!ffmpegExecutor->executeCommand(command, 0.0, 1.0))
    {
        if (logCallback)
            logCallback("ERROR: Failed to process clip " + sourceClip.getFileName());
        
        // If NVENC failed, try with CPU as fallback
        if (useNvidiaAcceleration)
        {
            if (logCallback)
                logCallback("  NVENC failed, trying CPU encoding as fallback");
            
            juce::String cpuEncoding = buildEncodingParams(tempCpuParams, false);
            command = 
                ffmpegExecutor->getFFmpegPath() + 
                " -y" +                               // Overwrite output
                " -i \"" + sourceClip.getFullPathName() + "\"";
                
            // Add seek offset if needed but ensure it's a valid value
            if (startTime > 0.0001) { // Only use if significantly greater than zero
                // Format with fixed precision to avoid scientific notation
                command += " -ss " + juce::String(startTime, 3);
                if (logCallback)
                    logCallback("  CPU fallback using start time: " + juce::String(startTime, 3) + " seconds");
            } 
            // No need to handle else case as we've already validated startTime
                
            // Use the already validated duration
            // We know duration is valid because we checked it earlier
            command += " -t " + juce::String(duration, 3);
            if (logCallback)
                logCallback("  CPU fallback using duration: " + juce::String(duration, 3) + " seconds");
            
            command += " ";
            command += cpuEncoding;
            command += " -pix_fmt yuv420p"; // Ensure compatibility with players
            command += " -an"; // No audio for clips
            command += " -movflags +faststart"; // Optimize for web streaming
            command += " \"";
            command += outputFile.getFullPathName();
            command += "\"";
            
            commandSucceeded = ffmpegExecutor->executeCommand(command, 0.0, 1.0);
            
            if (!commandSucceeded && logCallback)
                logCallback("ERROR: CPU fallback also failed for clip " + sourceClip.getFileName());
        }
    }
    else
    {
        commandSucceeded = true;
    }
    
    // Verify the output file was created correctly
    if (commandSucceeded && outputFile.existsAsFile() && outputFile.getSize() > 0)
    {
        if (logCallback)
            logCallback("Successfully processed clip: " + sourceClip.getFileName() + 
                     " to " + outputFile.getFileName());
        return true;
    }
    
    if (logCallback)
        logCallback("WARNING: Failed to create valid output file for clip: " + sourceClip.getFileName());
    
    return false;
}
