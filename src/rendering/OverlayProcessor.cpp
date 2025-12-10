#include "OverlayProcessor.h"

namespace
{
    juce::String sanitizeEncodingParams(const juce::String& input,
                                        bool useNvenc,
                                        const juce::String& fallback)
    {
        juce::String params = input.trim();
        if (params.isEmpty())
            params = fallback.trim();
        
        juce::StringArray tokens;
        tokens.addTokens(params, " ", "\"'");
        tokens.trim();
        tokens.removeEmptyStrings();
        
        juce::StringArray filtered;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const juce::String& token = tokens[i];
            
            if (token == "-c:v")
            {
                ++i;
                continue;
            }
            
            if (token.startsWith("-c:v"))
                continue;
            
            if (token == "-an")
                continue;
            
            auto skipOptionWithValue = [&](const juce::String& opt)
            {
                if (token == opt || token.startsWith(opt + "="))
                {
                    if (token == opt && i + 1 < tokens.size())
                        ++i; // Skip the value token
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

OverlayProcessor::OverlayProcessor(FFmpegExecutor* ffmpegExecutor)
    : ffmpegExecutor(ffmpegExecutor),
      useNvidiaAcceleration(false)
{
}

OverlayProcessor::~OverlayProcessor()
{
}

void OverlayProcessor::setLogCallback(std::function<void(const juce::String&)> callback)
{
    logCallback = callback;
}

void OverlayProcessor::setEncodingParams(bool nvAcceleration, 
                                      const juce::String& nvTempParams,
                                      const juce::String& cpuTempParams,
                                      const juce::String& nvFinalParams,
                                      const juce::String& cpuFinalParams)
{
    this->useNvidiaAcceleration = nvAcceleration;
    
    static const juce::String defaultTempNv = "-preset lossless -rc constqp -qp 0";
    static const juce::String defaultTempCpu = "-preset ultrafast -qp 0";
    static const juce::String defaultFinalNv = "-preset p5 -b:v 20M -maxrate 25M -bufsize 40M";
    static const juce::String defaultFinalCpu = "-preset medium -crf 18 -bufsize 20M";
    
    this->tempNvidiaParams = sanitizeEncodingParams(nvTempParams, true, defaultTempNv);
    this->tempCpuParams = sanitizeEncodingParams(cpuTempParams, false, defaultTempCpu);
    this->finalNvidiaParams = sanitizeEncodingParams(nvFinalParams, true, defaultFinalNv);
    this->finalCpuParams = sanitizeEncodingParams(cpuFinalParams, false, defaultFinalCpu);
}

bool OverlayProcessor::processOverlays(const juce::File& baseVideoFile,
                                    const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                    double totalDuration,
                                    const juce::File& tempDirectory,
                                    const juce::File& outputFile)
{
    if (logCallback)
        logCallback("Processing " + juce::String(overlayClips.size()) + " overlay clips");

    juce::File trimmedBaseVideo = baseVideoFile;
    double baseDuration = ffmpegExecutor->getFileDuration(baseVideoFile);

    // Trim base video if longer than target duration
    if (baseDuration > totalDuration + 0.1)
    {
        trimmedBaseVideo = tempDirectory.getChildFile("trimmed_base_video.mp4");

        juce::String trimCommand =
            ffmpegExecutor->getFFmpegPath() +
            " -y -i \"" + baseVideoFile.getFullPathName() + "\"" +
            " -t " + juce::String(totalDuration) +
            " -c:v copy -an \"" + trimmedBaseVideo.getFullPathName() + "\"";

        if (!ffmpegExecutor->executeCommand(trimCommand))
            trimmedBaseVideo = baseVideoFile;
    }
    
    if (overlayClips.empty())
    {
        juce::String copyCommand =
            ffmpegExecutor->getFFmpegPath() +
            " -y -i \"" + trimmedBaseVideo.getFullPathName() + "\"" +
            " -c copy \"" + outputFile.getFullPathName() + "\"";

        return ffmpegExecutor->executeCommand(copyCommand);
    }

    juce::File currentInput = trimmedBaseVideo;

    for (size_t i = 0; i < overlayClips.size(); ++i)
    {
        const auto& overlay = overlayClips[i];
        
        juce::File overlayOutput = tempDirectory.getChildFile("overlay_step_" + juce::String(i) + ".mp4");
        if (i == overlayClips.size() - 1)
            overlayOutput = outputFile;

        double startTime = overlay.startTimeSecs;
        double frequency = overlay.frequencySecs;
        double requestedDuration = overlay.duration;

        const double overlayFileDuration = ffmpegExecutor->getFileDuration(overlay.file);
        double overlayIterationDuration = requestedDuration > 0.0 ? requestedDuration : overlayFileDuration;
        if (overlayFileDuration > 0.0 && overlayIterationDuration > overlayFileDuration)
            overlayIterationDuration = overlayFileDuration;

        if (overlayIterationDuration <= 0.01)
        {
            if (logCallback)
                logCallback("WARNING: Overlay duration is invalid; skipping overlay application for this clip");

            juce::String copyCommand =
                ffmpegExecutor->getFFmpegPath() +
                " -y -i \"" + currentInput.getFullPathName() + "\" -c copy \"" + overlayOutput.getFullPathName() + "\"";

            if (!ffmpegExecutor->executeCommand(copyCommand))
                return false;

            if (i < overlayClips.size() - 1)
                currentInput = overlayOutput;

            continue;
        }

        juce::Array<double> appearanceStarts;
        juce::Array<double> appearanceDurations;
        double currentStart = startTime;
        const double effectiveFrequency = frequency > 0.0 ? frequency : overlayIterationDuration;
        const double tolerance = 0.0005;

        while (currentStart + overlayIterationDuration <= totalDuration + tolerance)
        {
            appearanceStarts.add(currentStart);
            appearanceDurations.add(overlayIterationDuration);

            if (frequency <= 0.0)
                break;

            currentStart += effectiveFrequency;
        }

        if (appearanceStarts.isEmpty())
        {
            if (logCallback)
                logCallback("Overlay timing produced zero appearances inside target duration - passing video through");

            juce::String copyCommand =
                ffmpegExecutor->getFFmpegPath() +
                " -y -i \"" + currentInput.getFullPathName() + "\" -c copy \"" + overlayOutput.getFullPathName() + "\"";

            if (!ffmpegExecutor->executeCommand(copyCommand))
                return false;

            if (i < overlayClips.size() - 1)
                currentInput = overlayOutput;

            continue;
        }


        const double overlayFps = juce::jmax(1.0, ffmpegExecutor->getVideoStreamInfo(overlay.file).fps);
        const int overlayFrameCount = overlayFileDuration > 0.0 ? (int)std::round(overlayFileDuration * overlayFps) : 0;

        juce::File initialOverlayInput = currentInput;
        juce::File sequentialInput = currentInput;
        const int appearanceCount = appearanceStarts.size();
        const bool isFinalOverlayClip = (i == overlayClips.size() - 1);

        // Always pre-render overlay timeline segments with alpha (QTRLE/ARGB), then overlay once.
        auto buildOverlaySegment = [&](double /*start*/, double duration, int idx, juce::File& outFile) -> bool
        {
            outFile = tempDirectory.getChildFile("overlay_seg_" + juce::String(i) + "_" + juce::String(idx) + ".mov");

            juce::String loopPrefix;
            if (overlayFrameCount > 0 && duration > overlayFileDuration + 0.001)
            {
                const double repetitionSeconds = overlayFileDuration > 0.0 ? overlayFileDuration : duration;
                const double repeatsNeeded = duration / repetitionSeconds;
                const int loopCount = (int)std::ceil(repeatsNeeded);
                loopPrefix = "loop=loop=" + juce::String(loopCount) + ":size=" + juce::String(overlayFrameCount) + ":start=0,";
            }

            juce::String filter =
                "[0:v]" + juce::String(loopPrefix) +
                "trim=start=0:end=" + juce::String(duration, 6) +
                ",setpts=PTS-STARTPTS,scale=1920:1080:force_original_aspect_ratio=decrease,format=rgba[ov];"
                "color=color=black:s=1920x1080:d=" + juce::String(duration, 6) + "[baseorig];"
                "[baseorig]format=rgba,colorchannelmixer=aa=0[base];"
                "[base][ov]overlay=x=(W-w)/2:y=(H-h)/2:format=auto,format=argb[out]";

            juce::String cmd = ffmpegExecutor->getFFmpegPath();
            cmd += " -y";
            cmd += " -i \"" + overlay.file.getFullPathName() + "\"";
            cmd += " -filter_complex \"" + filter + "\"";
            cmd += " -map \"[out]\"";
            cmd += " -t " + juce::String(duration, 6);
            cmd += " -c:v qtrle -pix_fmt argb -an \"" + outFile.getFullPathName() + "\"";

            return ffmpegExecutor->executeCommand(cmd, 0.0, 1.0);
        };

        auto buildGapSegment = [&](double duration, int idx, juce::File& outFile) -> bool
        {
            outFile = tempDirectory.getChildFile("overlay_gap_" + juce::String(i) + "_" + juce::String(idx) + ".mov");
            juce::String cmd = ffmpegExecutor->getFFmpegPath();
            cmd += " -y -f lavfi -i color=color=black:s=1920x1080:d=" + juce::String(duration, 6);
            cmd += " -vf format=rgba,colorchannelmixer=aa=0";
            cmd += " -c:v qtrle -pix_fmt argb -an \"" + outFile.getFullPathName() + "\"";
            return ffmpegExecutor->executeCommand(cmd, 0.0, 1.0);
        };

        juce::Array<juce::File> segments;
        double cursor = 0.0;
        for (int idx = 0; idx < appearanceCount; ++idx)
        {
            const double start = appearanceStarts[idx];
            const double dur = appearanceDurations[idx];

            const double gap = start - cursor;
            if (gap > 0.0001)
            {
                juce::File gapFile;
                if (!buildGapSegment(gap, segments.size(), gapFile))
                    return false;
                segments.add(gapFile);
                cursor += gap;
            }

            juce::File segFile;
            if (!buildOverlaySegment(start, dur, segments.size(), segFile))
                return false;
            segments.add(segFile);
            cursor += dur;
        }

        const double tailGap = totalDuration - cursor;
        if (tailGap > 0.0001)
        {
            juce::File gapFile;
            if (!buildGapSegment(tailGap, segments.size(), gapFile))
                return false;
            segments.add(gapFile);
        }

        juce::File concatList = tempDirectory.getChildFile("overlay_timeline_concat_" + juce::String(i) + ".txt");
        {
            juce::FileOutputStream out(concatList);
            if (!out.openedOk())
                return false;
            for (const auto& f : segments)
                out.writeText("file '" + f.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
            out.flush();
        }

        juce::File overlayTimeline = tempDirectory.getChildFile("overlay_timeline_" + juce::String(i) + ".mov");
        juce::String concatCmd = ffmpegExecutor->getFFmpegPath() +
            " -y -f concat -safe 0 -i \"" + concatList.getFullPathName() + "\" " +
            "-c:v qtrle -pix_fmt argb -an \"" + overlayTimeline.getFullPathName() + "\"";

        if (!ffmpegExecutor->executeCommand(concatCmd, 0.0, 1.0))
            return false;

        juce::File passOutput = isFinalOverlayClip ? overlayOutput
                                                   : tempDirectory.getChildFile("overlay_single_pass_" + juce::String(i) + ".mp4");

        juce::String overlayCmd = ffmpegExecutor->getFFmpegPath();
        overlayCmd += " -y";
        overlayCmd += " -i \"" + sequentialInput.getFullPathName() + "\"";
        overlayCmd += " -i \"" + overlayTimeline.getFullPathName() + "\"";
        overlayCmd += " -filter_complex \"[0:v][1:v]overlay=x=(W-w)/2:y=(H-h)/2:format=auto\"";
        overlayCmd += " -t " + juce::String(totalDuration);
        overlayCmd += " " + (useNvidiaAcceleration ? finalNvidiaParams : finalCpuParams);
        overlayCmd += " -pix_fmt yuv420p -an";
        overlayCmd += " \"" + passOutput.getFullPathName() + "\"";

        bool success = ffmpegExecutor->executeCommand(overlayCmd, 0.0, 1.0);

        if (!success && useNvidiaAcceleration)
        {
            if (logCallback)
                logCallback("WARNING: Overlay encoding failed with NVENC; retrying with CPU settings");
            overlayCmd = ffmpegExecutor->getFFmpegPath();
            overlayCmd += " -y";
            overlayCmd += " -i \"" + sequentialInput.getFullPathName() + "\"";
            overlayCmd += " -i \"" + overlayTimeline.getFullPathName() + "\"";
            overlayCmd += " -filter_complex \"[0:v][1:v]overlay=x=(W-w)/2:y=(H-h)/2:format=auto\"";
            overlayCmd += " -t " + juce::String(totalDuration);
            overlayCmd += " " + finalCpuParams;
            overlayCmd += " -pix_fmt yuv420p -an";
            overlayCmd += " \"" + passOutput.getFullPathName() + "\"";

            success = ffmpegExecutor->executeCommand(overlayCmd, 0.0, 1.0);
        }

        for (const auto& f : segments)
            if (f.existsAsFile()) f.deleteFile();
        if (concatList.existsAsFile()) concatList.deleteFile();
        if (!isFinalOverlayClip && overlayTimeline.existsAsFile()) overlayTimeline.deleteFile();

        if (!success)
        {
            if (logCallback)
                logCallback("ERROR: Overlay processing failed for " + overlay.file.getFileName());
            return false;
        }

        if (sequentialInput != passOutput && sequentialInput != initialOverlayInput && sequentialInput.existsAsFile())
            sequentialInput.deleteFile();

        if (i < overlayClips.size() - 1)
            currentInput = overlayOutput;
    }

    return true;
}

juce::File OverlayProcessor::prepareOverlayClip(const RenderTypes::OverlayClipInfo& overlayClip,
                                            const juce::File& tempDirectory)
{
    juce::String fileExt = overlayClip.file.getFileExtension().toLowerCase();

    if (fileExt == ".png")
    {
        juce::File preparedFile = tempDirectory.getChildFile(
            "overlay_prepared_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".mov");

        juce::String ffmpegPath = ffmpegExecutor->getFFmpegPath();
        juce::String command =
            "\"" + ffmpegPath + "\" -y " +
            "-loop 1 -i \"" + overlayClip.file.getFullPathName() + "\" " +
            "-c:v qtrle -pix_fmt argb " +
            "-t " + juce::String(overlayClip.duration) + " " +
            "\"" + preparedFile.getFullPathName() + "\"";

        if (ffmpegExecutor->executeCommand(command) && preparedFile.existsAsFile())
            return preparedFile;
    }
    else if (fileExt == ".mov")
    {
        juce::String ffprobePath = ffmpegExecutor->getFFmpegPath().replace("ffmpeg", "ffprobe");
        juce::ChildProcess process;

        juce::String command = "\"" + ffprobePath +
                          "\" -v error -select_streams v -show_entries stream=pix_fmt " +
                          "-of default=noprint_wrappers=1:nokey=1 \"" +
                          overlayClip.file.getFullPathName() + "\"";

        bool needsPreprocessing = false;

        if (process.start(command))
        {
            process.waitForProcessToFinish(3000);

            char buffer[256] = {0};
            const int bytesRead = process.readProcessOutput(buffer, sizeof(buffer) - 1);

            if (bytesRead > 0)
            {
                buffer[bytesRead] = 0;
                juce::String pixFmt = juce::String(juce::CharPointer_UTF8(buffer)).trim();
                needsPreprocessing = !pixFmt.contains("rgba") &&
                                    !pixFmt.contains("yuva") &&
                                    !pixFmt.contains("argb");
            }
        }

        if (needsPreprocessing)
        {
            juce::File preparedFile = tempDirectory.getChildFile(
                "overlay_prepared_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".mov");

            juce::String ffmpegPath = ffmpegExecutor->getFFmpegPath();
            juce::String convertCommand =
                "\"" + ffmpegPath + "\" -y " +
                "-i \"" + overlayClip.file.getFullPathName() + "\" " +
                "-c:v qtrle -pix_fmt argb " +
                "\"" + preparedFile.getFullPathName() + "\"";

            if (ffmpegExecutor->executeCommand(convertCommand) && preparedFile.existsAsFile())
                return preparedFile;
        }
    }

    return juce::File();
}
