#include "TimelineAssembler.h"

namespace
{
    juce::String sanitizeEncodingString(const juce::String& input,
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
            juce::String token = tokens[i];
            
            auto skipOptionWithValue = [&](const juce::String& opt)
            {
                if (token == opt || token.startsWith(opt + "="))
                {
                    if (token == opt && i + 1 < tokens.size())
                        ++i;
                    return true;
                }
                return false;
            };
            
            if (token == "-c:v" || token.startsWith("-c:v"))
            {
                if (token == "-c:v" && i + 1 < tokens.size())
                    ++i;
                continue;
            }
            
            if (skipOptionWithValue("-profile") ||
                skipOptionWithValue("-profile:v") ||
                skipOptionWithValue("-level") ||
                skipOptionWithValue("-level:v") ||
                skipOptionWithValue("-pix_fmt") ||
                skipOptionWithValue("-movflags") ||
                skipOptionWithValue("-an"))
            {
                continue;
            }
            
            filtered.add(token);
        }
        
        juce::String result = "-c:v " + juce::String(useNvenc ? "h264_nvenc" : "libx264");
        juce::String extras = filtered.joinIntoString(" ");
        if (extras.isNotEmpty())
            result += " " + extras;
        return result.trim();
    }
}

TimelineAssembler::TimelineAssembler(FFmpegExecutor* ffmpegExecutor, OverlayProcessor* overlayProcessor)
    : ffmpegExecutor(ffmpegExecutor),
      overlayProcessor(overlayProcessor),
      useNvidiaAcceleration(false),
      tempNvidiaParams("-c:v h264_nvenc -preset lossless -rc constqp -qp 0"),
      tempCpuParams("-c:v libx264 -preset ultrafast -qp 0"),  // Lossless H.264
      finalNvidiaParams("-preset p5 -b:v 20M -maxrate 25M -bufsize 40M"),  // Higher quality final output
      finalCpuParams("-preset medium -crf 18 -bufsize 20M"),  // Better quality for CPU encoding
      losslessParams("-c:v libx264 -preset ultrafast -qp 0")  // Lossless intermediate encoding
{
}

TimelineAssembler::~TimelineAssembler()
{
}

void TimelineAssembler::setLogCallback(std::function<void(const juce::String&)> logCallback)
{
    this->logCallback = logCallback;
}

void TimelineAssembler::setEncodingParams(bool useNvidiaAcceleration, 
                                       const juce::String& tempNvidiaParams,
                                       const juce::String& tempCpuParams,
                                       const juce::String& finalNvidiaParams,
                                       const juce::String& finalCpuParams)
{
    this->useNvidiaAcceleration = useNvidiaAcceleration;
    static const juce::String defaultTempNv = "-preset lossless -rc constqp -qp 0";
    static const juce::String defaultTempCpu = "-preset ultrafast -qp 0";
    static const juce::String defaultFinalNv = "-preset p5 -b:v 20M -maxrate 25M -bufsize 40M";
    static const juce::String defaultFinalCpu = "-preset medium -crf 18 -bufsize 20M";

    this->tempNvidiaParams = sanitizeEncodingString(tempNvidiaParams, true, defaultTempNv);
    this->tempCpuParams = sanitizeEncodingString(tempCpuParams, false, defaultTempCpu);
    this->finalNvidiaParams = sanitizeEncodingString(finalNvidiaParams, true, defaultFinalNv);
    this->finalCpuParams = sanitizeEncodingString(finalCpuParams, false, defaultFinalCpu);
}

bool TimelineAssembler::assembleTimeline(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                      const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                      const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                      const juce::File& audioFile,
                                      double targetDuration,
                                      const juce::File& tempDirectory,
                                      const juce::File& outputFile,
                                      double fadeInDuration,
                                      double fadeOutDuration)
{
    try {
        auto setProgressSlice = [&](double start, double end, double estSeconds)
        {
            if (ffmpegExecutor)
                ffmpegExecutor->setExternalProgressWindow(start, end, estSeconds);
        };

        auto clearProgressSlice = [&]()
        {
            if (ffmpegExecutor)
                ffmpegExecutor->clearExternalProgressWindow();
        };

        // Store fade durations for final muxing
        this->fadeInDuration = fadeInDuration;
        this->fadeOutDuration = fadeOutDuration;
        
        if (logCallback) {
            logCallback("========== TIMELINE ASSEMBLY ALGORITHM ==========");
            logCallback("Following algorithm.md specification exactly");
            logCallback("Target duration: " + juce::String(targetDuration) + " seconds");
            logCallback("Fade in duration: " + juce::String(fadeInDuration) + " seconds");
            logCallback("Fade out duration: " + juce::String(fadeOutDuration) + " seconds");
            logCallback("Intro clips: " + juce::String(introClips.size()));
            logCallback("Loop clips: " + juce::String(loopClips.size()));
            logCallback("Overlay clips: " + juce::String(overlayClips.size()));
        }
        
        // Store the total duration for access in other methods
        this->totalDuration = targetDuration;
        
        // Step 1: Conform clips
        setProgressSlice(0.00, 0.10, targetDuration);
        if (!conformInputClips(introClips, loopClips, tempDirectory)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        // Step 2: Generate crossfades
        setProgressSlice(0.10, 0.25, targetDuration);
        if (!generateCrossfadeComponents(introClips, loopClips, tempDirectory)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        // Step 3: Assemble intro sequence
        setProgressSlice(0.25, 0.35, targetDuration);
        if (!assembleIntroSequence(introClips, tempDirectory)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        // Step 4: Assemble loop sequence
        setProgressSlice(0.35, 0.55, targetDuration);
        if (!assembleLoopSequence(introClips, loopClips, tempDirectory)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        // Step 5: Final loop assembly
        setProgressSlice(0.55, 0.65, targetDuration);
        if (!finalLoopSequenceAssembly(tempDirectory, introClips, loopClips)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        // Step 6: Process overlays
        setProgressSlice(0.65, 0.90, targetDuration);
        if (!calculateFinalAssemblyAndProcessOverlays(overlayClips, targetDuration, tempDirectory)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();
        
        // Step 7: Mux final output
        setProgressSlice(0.90, 1.00, targetDuration);
        if (!muxFinalOutput(audioFile, tempDirectory, outputFile)) {
            clearProgressSlice();
            return false;
        }
        clearProgressSlice();

        return true;
    }
    catch (const std::exception& e) {
        if (logCallback)
            logCallback("EXCEPTION: Error in assembleTimeline: " + juce::String(e.what()));
        return false;
    }
    catch (...) {
        if (logCallback)
            logCallback("EXCEPTION: Unknown error in assembleTimeline");
        return false;
    }
}

// STEP 1: Conform Input Clips to Defined Durations
bool TimelineAssembler::conformInputClips(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                        const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                        const juce::File& tempDirectory)
{
    if (logCallback) logCallback("Conforming input clips to defined durations...");

    auto buildCommand = [](FFmpegExecutor* executor,
                           const juce::File& source,
                           const juce::File& destination,
                           double clipDuration,
                           double safeStartTime,
                           double effectiveSourceDuration,
                           const juce::String& encodingParams) -> juce::String
    {
        juce::String cmd = executor->getFFmpegPath() + " -y";

        if (clipDuration > effectiveSourceDuration + 0.1)
        {
            int loopCount = (int)std::ceil(clipDuration / effectiveSourceDuration);
            cmd += " -stream_loop " + juce::String(loopCount - 1) +
                   " -i \"" + source.getFullPathName() + "\"";
        }
        else
        {
            cmd += " -i \"" + source.getFullPathName() + "\"";
        }

        if (safeStartTime > 0.001)
            cmd += " -ss " + juce::String(safeStartTime);

        cmd += " -t " + juce::String(clipDuration) +
               " " + encodingParams +
               " -pix_fmt yuv420p -an \"" + destination.getFullPathName() + "\"";

        return cmd;
    };

    auto ensureCodecParam = [&](const juce::String& params, bool nvenc) -> juce::String
    {
        juce::StringArray tokens;
        tokens.addTokens(params, " ", "\"'");
        tokens.trim();
        tokens.removeEmptyStrings();

        juce::StringArray filtered;
        for (int i = 0; i < tokens.size(); ++i)
        {
            juce::String token = tokens[i];

            auto skipOptionWithValue = [&](const juce::String& opt)
            {
                if (token == opt || token.startsWith(opt + "="))
                {
                    if (token == opt && i + 1 < tokens.size())
                        ++i;
                    return true;
                }
                return false;
            };

            // Strip codec, profile, level, pix_fmt, movflags, audio disable flags; we control these explicitly
            if (token == "-c:v" || token.startsWith("-c:v"))
            {
                if (token == "-c:v" && i + 1 < tokens.size())
                    ++i;
                continue;
            }

            if (skipOptionWithValue("-profile") ||
                skipOptionWithValue("-profile:v") ||
                skipOptionWithValue("-level") ||
                skipOptionWithValue("-level:v") ||
                skipOptionWithValue("-pix_fmt") ||
                skipOptionWithValue("-movflags") ||
                skipOptionWithValue("-an"))
            {
                continue;
            }

            filtered.add(token);
        }

        juce::String result = "-c:v " + juce::String(nvenc ? "h264_nvenc" : "libx264");
        const juce::String extras = filtered.joinIntoString(" ");
        if (extras.isNotEmpty())
            result += " " + extras;
        return result.trim();
    };

    auto conformClip = [&](const RenderTypes::VideoClipInfo& clip,
                           const juce::File& inputFile,
                           const juce::File& outputFile,
                           const juce::String& label) -> bool
    {
        if (!inputFile.existsAsFile()) {
            if (logCallback) logCallback("ERROR: Input file not found: " + inputFile.getFullPathName());
            return false;
        }

        double safeStartTime = (clip.startTime > 0.001 && clip.startTime < 1e10) ? clip.startTime : 0.0;
        if (logCallback && clip.startTime != safeStartTime)
            logCallback("WARNING: Invalid startTime " + juce::String(clip.startTime) + " corrected to " + juce::String(safeStartTime));

        double sourceDuration = ffmpegExecutor->getFileDuration(inputFile);
        double effectiveSourceDuration = sourceDuration - safeStartTime;

        if (logCallback)
        {
            logCallback("  - Source duration: " + juce::String(sourceDuration) + "s");
            logCallback("  - Effective source duration (after start time): " + juce::String(effectiveSourceDuration) + "s");
            logCallback("  - Target duration: " + juce::String(clip.duration) + "s");
        }

        juce::String primaryParams = ensureCodecParam(useNvidiaAcceleration ? tempNvidiaParams : tempCpuParams,
                                                      useNvidiaAcceleration);
        juce::String command = buildCommand(ffmpegExecutor, inputFile, outputFile, clip.duration, safeStartTime, effectiveSourceDuration, primaryParams);

        if (!ffmpegExecutor->executeCommand(command, 0.0, 1.0))
        {
            if (useNvidiaAcceleration)
            {
                if (logCallback) logCallback("WARNING: NVENC conform failed for " + label + ", retrying with CPU preset");
                juce::String fallbackParams = ensureCodecParam(tempCpuParams, false);
                juce::String fallbackCommand = buildCommand(ffmpegExecutor, inputFile, outputFile, clip.duration, safeStartTime, effectiveSourceDuration, fallbackParams);
                if (!ffmpegExecutor->executeCommand(fallbackCommand, 0.0, 1.0))
                {
                    if (logCallback) logCallback("ERROR: CPU fallback also failed for " + label);
                    return false;
                }
            }
            else
            {
                if (logCallback) logCallback("ERROR: Failed to conform " + label);
                return false;
            }
        }

        return true;
    };

    for (size_t i = 0; i < introClips.size(); ++i)
    {
        const auto& clip = introClips[i];
        juce::File inputFile = clip.file;
        juce::File outputFile = tempDirectory.getChildFile("intro_" + juce::String(i) + ".mp4");

        if (!conformClip(clip, inputFile, outputFile, "intro_" + juce::String(i)))
        {
            if (logCallback) logCallback("ERROR: Failed to conform intro clip " + juce::String(i));
            return false;
        }

        if (logCallback) logCallback("Conformed intro_" + juce::String(i) + ": " + juce::String(clip.duration) + "s");
    }

    for (size_t i = 0; i < loopClips.size(); ++i)
    {
        const auto& clip = loopClips[i];
        juce::File inputFile = clip.file;
        juce::File outputFile = tempDirectory.getChildFile("loop_" + juce::String(i) + ".mp4");

        if (!conformClip(clip, inputFile, outputFile, "loop_" + juce::String(i)))
        {
            if (logCallback) logCallback("ERROR: Failed to conform loop clip " + juce::String(i));
            return false;
        }

        if (logCallback) logCallback("Conformed loop_" + juce::String(i) + ": " + juce::String(clip.duration) + "s (from file: " + clip.file.getFileName() + ")");
    }

    return true;
}

// STEP 2: Generate Crossfade Components Between Clips
bool TimelineAssembler::generateCrossfadeComponents(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                                  const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                                  const juce::File& tempDirectory)
{
    if (logCallback) logCallback("Generating crossfade components between clips...");
    
    // Generate intro crossfades (if more than one intro clip)
    if (introClips.size() > 1) {
        for (size_t i = 0; i < introClips.size() - 1; i++) {
            if (introClips[i].crossfade > 0.001) {
                // Generate crossfade segments and transition
                if (!generateCrossfadeForClipPair("intro", i, i+1, introClips[i], introClips[i+1], tempDirectory)) {
                    return false;
                }
                
                // For middle clips (clips that have crossfades on both sides), 
                // create the body_cut_in_cut_out segment
                if (i + 1 < introClips.size() - 1 && introClips[i+1].crossfade > 0.001) {
                    // Clip at index i+1 is a middle clip - create body_cut_in_cut_out
                    if (!createMiddleClipBodySegment("intro", i+1, introClips[i].crossfade, introClips[i+1].crossfade, tempDirectory)) {
                        return false;
                    }
                }
            }
        }
    }
    
    // Generate loop crossfades (if more than one loop clip)
    if (loopClips.size() > 1) {
        for (size_t i = 0; i < loopClips.size() - 1; i++) {
            if (loopClips[i].crossfade > 0.001) {
                // Generate crossfade segments and transition
                if (!generateCrossfadeForClipPair("loop", i, i+1, loopClips[i], loopClips[i+1], tempDirectory)) {
                    return false;
                }
                
                // For middle clips (clips that have crossfades on both sides), 
                // create the body_cut_in_cut_out segment
                if (i + 1 < loopClips.size() - 1 && loopClips[i+1].crossfade > 0.001) {
                    // Clip at index i+1 is a middle clip - create body_cut_in_cut_out
                    if (!createMiddleClipBodySegment("loop", i+1, loopClips[i].crossfade, loopClips[i+1].crossfade, tempDirectory)) {
                        return false;
                    }
                }
            }
        }
    }
    
    // NOTE: Sequence-to-sequence crossfades are generated in Step 5 after extracting segments
    
    return true;
}

// STEP 3: Assemble the Intro Sequence
bool TimelineAssembler::assembleIntroSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                            const juce::File& tempDirectory)
{
    if (introClips.empty()) {
        if (logCallback) logCallback("No intro clips to assemble");
        return true;
    }
    
    if (logCallback) logCallback("Assembling intro sequence...");
    
    // Build intro_sequence_raw by concatenating components
    juce::File introSequenceRaw = tempDirectory.getChildFile("intro_sequence_raw.mp4");
    if (!buildRawSequence("intro", introClips, tempDirectory, introSequenceRaw)) {
        return false;
    }
    
    // Trim the final crossfade to create intro_sequence
    juce::File introSequence = tempDirectory.getChildFile("intro_sequence.mp4");
    juce::File loopFromIntroSequenceXOut = tempDirectory.getChildFile("loop_from_intro_sequence_x_out.mp4");
    
    double lastCrossfadeDuration = introClips.back().crossfade;
    
    const bool hasCrossfadeTail = lastCrossfadeDuration > 0.001;

    if (hasCrossfadeTail) {
        if (logCallback) {
            if (introClips.size() == 1)
                logCallback("Single intro clip but crossfade > 0s - trimming tail for intro-to-loop transition");
            else
                logCallback("Trimming intro sequence tail for final crossfade segment");
        }

        if (!trimFinalCrossfade(introSequenceRaw, introSequence, loopFromIntroSequenceXOut, lastCrossfadeDuration)) {
            return false;
        }
    } else {
        // No crossfade to trim, just copy
        if (!copyFile(introSequenceRaw, introSequence)) {
            return false;
        }
    }
    
    // Delete raw sequence
    introSequenceRaw.deleteFile();
    
    if (logCallback) logCallback("Intro sequence assembled successfully");
    return true;
}

// STEP 4: Assemble the Loop Sequence
bool TimelineAssembler::assembleLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                           const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                           const juce::File& tempDirectory)
{
    if (loopClips.empty()) {
        if (logCallback) logCallback("No loop clips to assemble");
        return true;
    }
    
    if (logCallback) logCallback("Assembling loop sequence...");
    
    // Build loop_sequence_raw
    juce::File loopSequenceRaw = tempDirectory.getChildFile("loop_sequence_raw.mp4");
    if (!buildRawSequence("loop", loopClips, tempDirectory, loopSequenceRaw)) {
        return false;
    }
    
    // Step 4: Extract and Process Variants according to algorithm.md
    if (!extractLoopVariants(loopSequenceRaw, tempDirectory, "intro_based", introClips, loopClips)) {
        return false;
    }
    
    if (!extractLoopVariants(loopSequenceRaw, tempDirectory, "loop_based", introClips, loopClips)) {
        return false;
    }
    
    if (logCallback) logCallback("Created basic loop sequences");
    
    // Delete raw sequence
    loopSequenceRaw.deleteFile();
    
    if (logCallback) logCallback("Loop sequence assembled successfully");
    return true;
}

// STEP 5: Final Loop Sequence Assembly
bool TimelineAssembler::finalLoopSequenceAssembly(const juce::File& tempDirectory,
                                                  const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                                  const std::vector<RenderTypes::VideoClipInfo>& loopClips)
{
    if (logCallback) logCallback("Performing final loop sequence assembly...");
    
    // First generate the interpolated crossfades between sequence parts
    if (!generateInterpolatedCrossfades(tempDirectory, introClips, loopClips)) {
        if (logCallback) logCallback("ERROR: Failed to generate interpolated crossfades");
        return false;
    }
    
    // Assemble loop_from_intro_sequence
    juce::File loopFromIntroSequenceX = tempDirectory.getChildFile("loop_from_intro_sequence_x.mp4");
    juce::File loopFromIntroBodyCutInCutOut = tempDirectory.getChildFile("loop_from_intro_body_cut_in_cut_out.mp4");
    juce::File loopFromIntroSequence = tempDirectory.getChildFile("loop_from_intro_sequence.mp4");
    
    // Check if intro crossfade was actually created (crossfade duration > 0)
    if (loopFromIntroSequenceX.existsAsFile()) {
        // Normal case: crossfade exists, concatenate crossfade + body
        if (!concatenateTwoFiles(loopFromIntroSequenceX, loopFromIntroBodyCutInCutOut, loopFromIntroSequence)) {
            return false;
        }
        if (logCallback) logCallback("Concatenated intro crossfade + body to create final intro sequence");
    } else {
        // Zero crossfade case: just copy the body file directly (no crossfade needed)
        if (!copyFile(loopFromIntroBodyCutInCutOut, loopFromIntroSequence)) {
            if (logCallback) logCallback("ERROR: Failed to copy intro body as final sequence");
            return false;
        }
        if (logCallback) logCallback("Single intro clip - copied body directly as final intro sequence (no crossfade)");
    }
    
    // Assemble final loop_from_loop_sequence: [crossfade] + [body]
    juce::File loopFromLoopSequenceX = tempDirectory.getChildFile("loop_from_loop_sequence_x.mp4");
    juce::File loopFromLoopBodyCutInCutOut = tempDirectory.getChildFile("loop_from_loop_body_cut_in_cut_out.mp4");
    juce::File loopFromLoopSequence = tempDirectory.getChildFile("loop_from_loop_sequence.mp4");

    if (loopFromLoopSequenceX.existsAsFile()) {
        // Normal case: crossfade exists, concatenate crossfade + body
        if (!concatenateTwoFiles(loopFromLoopSequenceX, loopFromLoopBodyCutInCutOut, loopFromLoopSequence)) {
            return false;
        }
        if (logCallback) logCallback("Concatenating loop_from_loop_sequence_x.mp4 + loop_from_loop_body_cut_in_cut_out.mp4");
    } else {
        // Zero crossfade case: just copy the body file directly (no crossfade needed)
        if (!loopFromLoopBodyCutInCutOut.existsAsFile()) {
            if (logCallback) logCallback("ERROR: Loop body file does not exist for seamless loop");
            return false;
        }
        if (!loopFromLoopBodyCutInCutOut.copyFileTo(loopFromLoopSequence)) {
            if (logCallback) logCallback("ERROR: Failed to copy loop body for seamless loop (no crossfade)");
            return false;
        }
        if (logCallback) logCallback("No crossfade needed - copying loop_from_loop_body_cut_in_cut_out.mp4 directly");
    }
    
    if (logCallback) logCallback("Final loop sequence assembly completed");
    return true;
}

// STEP 6: Calculate Final Assembly and Process Overlays
bool TimelineAssembler::calculateFinalAssemblyAndProcessOverlays(const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                                               double targetDuration,
                                                               const juce::File& tempDirectory)
{
    if (logCallback) logCallback("Calculating final assembly and processing overlays...");
    
    auto deleteIfExists = [&](const juce::File& file, const juce::String& label)
    {
        if (file.existsAsFile())
        {
            if (logCallback) logCallback("Deleting " + label + ": " + file.getFileName());
            file.deleteFile();
        }
    };
    
    auto cleanupClipIntermediates = [&]()
    {
        for (juce::DirectoryIterator it(tempDirectory, false, "*.mp4", juce::File::findFiles); it.next();)
        {
            const juce::String name = it.getFile().getFileName();
            
            // Drop heavy clip-level intermediates once final sequences exist
            const bool isLoopClip = name.startsWith("loop_") && !name.startsWith("loop_from_");
            const bool isPreparedClip = name.startsWith("loop_clip_") || name.startsWith("intro_clip_");
            const bool isRawSequence = name == "loop_sequence_raw.mp4" || name == "intro_sequence_raw.mp4";
            
            if (isLoopClip || isPreparedClip || isRawSequence)
                deleteIfExists(it.getFile(), "intermediate clip");
        }
    };
    
    // Measure sequence durations
    juce::File introSequence = tempDirectory.getChildFile("intro_sequence.mp4");
    juce::File loopFromIntroSequence = tempDirectory.getChildFile("loop_from_intro_sequence.mp4");
    juce::File loopFromLoopSequence = tempDirectory.getChildFile("loop_from_loop_sequence.mp4");
    
    double introSeqDuration = introSequence.existsAsFile() ? ffmpegExecutor->getFileDuration(introSequence) : 0.0;
    double loopFromIntroDuration = loopFromIntroSequence.existsAsFile() ? ffmpegExecutor->getFileDuration(loopFromIntroSequence) : 0.0;
    double loopFromLoopDuration = loopFromLoopSequence.existsAsFile() ? ffmpegExecutor->getFileDuration(loopFromLoopSequence) : 0.0;
    
    // Free clip-level intermediates now that we have the assembled sequences
    cleanupClipIntermediates();
    
    // Calculate repetition count
    double remainingDuration = targetDuration - introSeqDuration - loopFromIntroDuration;
    int x = (remainingDuration > 0 && loopFromLoopDuration > 0) ? 
           (int)std::ceil(remainingDuration / loopFromLoopDuration) : 0;
    
    if (logCallback) {
        logCallback("Sequence durations - Intro: " + juce::String(introSeqDuration) + 
                   "s, Loop from intro: " + juce::String(loopFromIntroDuration) + 
                   "s, Loop from loop: " + juce::String(loopFromLoopDuration) + "s");
        logCallback("Loop repetitions needed: " + juce::String(x));
    }
    
    // Build provisional final sequence
    juce::File provisionalSequence = tempDirectory.getChildFile("provisional_sequence.mp4");
    if (!buildProvisionalSequence(introSequence, loopFromIntroSequence, loopFromLoopSequence, x, provisionalSequence)) {
        return false;
    }
    
    // Trim to exact duration
    juce::File outputSequenceWithoutOverlays = tempDirectory.getChildFile("output_sequence_without_overlays.mp4");
    if (!trimToExactDuration(provisionalSequence, outputSequenceWithoutOverlays, targetDuration)) {
        return false;
    }
    
    // Provisional sequence is huge; delete once the trimmed version exists
    deleteIfExists(provisionalSequence, "provisional sequence");
    deleteIfExists(tempDirectory.getChildFile("provisional_concat.txt"), "provisional concat list");
    
    // Apply overlays if they exist
    if (!overlayClips.empty()) {
        juce::File outputSequenceWithOverlays = tempDirectory.getChildFile("output_sequence_with_overlays.mp4");
        if (!applyOverlays(outputSequenceWithoutOverlays, overlayClips, outputSequenceWithOverlays)) {
            return false;
        }
        
        if (outputSequenceWithOverlays.existsAsFile()) {
            outputSequenceWithoutOverlays.deleteFile();
            outputSequenceWithOverlays.moveFileTo(outputSequenceWithoutOverlays);
        }
    }
    
    if (logCallback) logCallback("Final assembly and overlay processing completed");
    return true;
}

// STEP 7: Mux Final Output
bool TimelineAssembler::muxFinalOutput(const juce::File& audioFile,
                                     const juce::File& tempDirectory,
                                     const juce::File& outputFile)
{
    if (logCallback) logCallback("Muxing final output...");
    
    // Determine which video sequence to use
    juce::File videoSequence = tempDirectory.getChildFile("output_sequence_with_overlays.mp4");
    if (!videoSequence.existsAsFile()) {
        videoSequence = tempDirectory.getChildFile("output_sequence_without_overlays.mp4");
    }
    
    if (!videoSequence.existsAsFile()) {
        if (logCallback) logCallback("ERROR: No final video sequence found");
        return false;
    }
    
    double actualVideoDuration = ffmpegExecutor->getVideoDuration(videoSequence);
    
    // Validate fade parameters
    if (actualVideoDuration > 0 && fadeOutDuration > actualVideoDuration) {
        if (logCallback) {
            logCallback("WARNING: Fade out duration (" + juce::String(fadeOutDuration) + 
                       "s) exceeds video duration (" + juce::String(actualVideoDuration) + 
                       "s). Adjusting fade out duration.");
        }
        fadeOutDuration = actualVideoDuration * 0.1; // Use 10% of video duration
    }
    
    // Build fade filters for audio and video
    juce::String audioFadeFilter = "";
    juce::String videoFadeFilter = "";
    
    if (fadeInDuration > 0.001 || fadeOutDuration > 0.001) {
        if (logCallback) {
            logCallback("Applying fade effects - In: " + juce::String(fadeInDuration) + "s, Out: " + juce::String(fadeOutDuration) + "s");
        }
        
        // Audio fade filter
        juce::String audioFilters;
        if (fadeInDuration > 0.001) {
            audioFilters += "afade=in:st=0:d=" + juce::String(fadeInDuration);
        }
        if (fadeOutDuration > 0.001) {
            if (!audioFilters.isEmpty()) audioFilters += ",";
            // Use actual video duration if available, otherwise use totalDuration
            double effectiveDuration = (actualVideoDuration > 0) ? actualVideoDuration : totalDuration;
            double fadeOutStart = effectiveDuration - fadeOutDuration;
            
            // Ensure fade out doesn't start before 0
            if (fadeOutStart < 0) {
                if (logCallback) logCallback("WARNING: Fade out start time negative, adjusting to 0");
                fadeOutStart = 0;
            }
            
            // For very long videos, use a more precise format to avoid scientific notation
            juce::String fadeOutStartStr = juce::String(fadeOutStart, 6);
            juce::String fadeOutDurationStr = juce::String(fadeOutDuration, 6);
            
            audioFilters += "afade=out:st=" + fadeOutStartStr + ":d=" + fadeOutDurationStr;
        }
        if (!audioFilters.isEmpty()) {
            audioFadeFilter = " -af \"" + audioFilters + "\"";
        }
        
        // Video fade filter  
        juce::String videoFilters;
        if (fadeInDuration > 0.001) {
            videoFilters += "fade=in:st=0:d=" + juce::String(fadeInDuration);
        }
        if (fadeOutDuration > 0.001) {
            if (!videoFilters.isEmpty()) videoFilters += ",";
            // Use actual video duration if available, otherwise use totalDuration
            double effectiveDuration = (actualVideoDuration > 0) ? actualVideoDuration : totalDuration;
            double fadeOutStart = effectiveDuration - fadeOutDuration;
            
            // Ensure fade out doesn't start before 0
            if (fadeOutStart < 0) {
                if (logCallback) logCallback("WARNING: Fade out start time negative, adjusting to 0");
                fadeOutStart = 0;
            }
            
            // For very long videos, use a more precise format to avoid scientific notation
            juce::String fadeOutStartStr = juce::String(fadeOutStart, 6);
            juce::String fadeOutDurationStr = juce::String(fadeOutDuration, 6);
            
            videoFilters += "fade=out:st=" + fadeOutStartStr + ":d=" + fadeOutDurationStr;
        }
        if (!videoFilters.isEmpty()) {
            videoFadeFilter = " -vf \"" + videoFilters + "\"";
        }
    }
    
    // Build mux command with fade effects and 44100Hz audio
    juce::String encodingParams = useNvidiaAcceleration ? finalNvidiaParams : finalCpuParams;
    
    // For very long videos with fades, use complex filter to avoid timing issues
    juce::String command;
    if ((fadeInDuration > 0.001 || fadeOutDuration > 0.001) && totalDuration > 3600) {
        // Use complex filter for long videos with fades
        juce::String complexFilter = "";
        
        // Build complex filter string
        if (!videoFadeFilter.isEmpty() && !audioFadeFilter.isEmpty()) {
            // Extract filter values from the filter strings
            juce::String videoFilterValue = videoFadeFilter.substring(6, videoFadeFilter.length() - 1); // Remove -vf " and "
            juce::String audioFilterValue = audioFadeFilter.substring(6, audioFadeFilter.length() - 1); // Remove -af " and "
            
            // Add LUFS normalization to audio chain
            complexFilter = " -filter_complex \"[0:v]" + videoFilterValue + "[v];[1:a]" + audioFilterValue + ",loudnorm=I=-14:TP=-1:LRA=11[a]\"" +
                           " -map \"[v]\" -map \"[a]\"";
        } else if (!videoFadeFilter.isEmpty()) {
            // Video fade only, add audio normalization
            complexFilter = videoFadeFilter + " -filter_complex \"[1:a]loudnorm=I=-14:TP=-1:LRA=11[a]\" -map 0:v -map \"[a]\"";
        } else if (!audioFadeFilter.isEmpty()) {
            // Audio fade only, add normalization to fade chain
            juce::String audioFilterValue = audioFadeFilter.substring(6, audioFadeFilter.length() - 1); // Remove -af " and "
            complexFilter = " -filter_complex \"[1:a]" + audioFilterValue + ",loudnorm=I=-14:TP=-1:LRA=11[a]\" -map 0:v -map \"[a]\"";
        } else {
            // No fades, just normalize audio
            complexFilter = " -filter_complex \"[1:a]loudnorm=I=-14:TP=-1:LRA=11[a]\" -map 0:v -map \"[a]\"";
        }
        
        command = ffmpegExecutor->getFFmpegPath() + 
            " -y" +
            " -i \"" + videoSequence.getFullPathName() + "\"" +
            " -i \"" + audioFile.getFullPathName() + "\"" +
            complexFilter +
            " " + encodingParams +  // Lossless encoding already includes codec
            " -c:a aac -ar 48000 -b:a 384k" +  // YouTube recommended: 48kHz, 384kbps for stereo
            " -pix_fmt yuv420p" +
            (useNvidiaAcceleration ? " -profile:v high" : " -profile:v high -level 4.0") +  // YouTube recommends High Profile
            " -movflags +faststart" +
            " \"" + outputFile.getFullPathName() + "\"";
    } else {
        // Standard command for shorter videos or no fades
        command = ffmpegExecutor->getFFmpegPath() + 
            " -y" +
            " -i \"" + videoSequence.getFullPathName() + "\"" +
            " -i \"" + audioFile.getFullPathName() + "\"" +
            " -map 0:v -map 1:a" +
            videoFadeFilter +
            " " + encodingParams +  // Lossless encoding already includes codec
            " -c:a aac -ar 48000 -b:a 384k" +  // YouTube recommended: 48kHz, 384kbps for stereo
            (audioFadeFilter.isEmpty() ? " -filter:a \"loudnorm=I=-14:TP=-1:LRA=11\"" : 
             audioFadeFilter.dropLastCharacters(1) + ",loudnorm=I=-14:TP=-1:LRA=11\"") +  // Add LUFS normalization
            " -pix_fmt yuv420p" +
            (useNvidiaAcceleration ? " -profile:v high" : " -profile:v high -level 4.0") +  // YouTube recommends High Profile
            " -movflags +faststart" +
            " \"" + outputFile.getFullPathName() + "\"";
    }
    
    
    if (!ffmpegExecutor->executeCommand(command, 0.0, 1.0)) {
        if (logCallback)
            logCallback("ERROR: Failed to mux final output");
        return false;
    }
    
    // Delete temporary video sequence files
    tempDirectory.getChildFile("output_sequence_with_overlays.mp4").deleteFile();
    tempDirectory.getChildFile("output_sequence_without_overlays.mp4").deleteFile();
    
    if (logCallback) logCallback("Final output muxed successfully: " + outputFile.getFullPathName());
    return true;
}

bool TimelineAssembler::generateCrossfadeForClipPair(const juce::String& type, size_t fromIndex, size_t toIndex,
                                                    const RenderTypes::VideoClipInfo& fromClip, const RenderTypes::VideoClipInfo& toClip,
                                                    const juce::File& tempDirectory)
{
    double crossfadeDuration = fromClip.crossfade;
    
    if (crossfadeDuration <= 0.001) {
        if (logCallback) logCallback("No crossfade needed (duration = " + juce::String(crossfadeDuration) + "s)");
        return true; // No crossfade needed
    }
    
    // Get source clips - these are individual conformed clips
    juce::File fromClipFile = tempDirectory.getChildFile(type + "_" + juce::String(fromIndex) + ".mp4");
    juce::File toClipFile = tempDirectory.getChildFile(type + "_" + juce::String(toIndex) + ".mp4");
    
    if (logCallback) {
        logCallback("  - From clip: " + fromClipFile.getFileName());
        logCallback("  - To clip: " + toClipFile.getFileName());
    }
    
    if (!fromClipFile.existsAsFile() || !toClipFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Source clips not found for crossfade");
        return false;
    }
    
    // Extract crossfade-out segment from fromClip (last n seconds)
    juce::File fromXOut = tempDirectory.getChildFile(type + "_" + juce::String(fromIndex) + "_x_out.mp4");
    double fromActualDuration = ffmpegExecutor->getFileDuration(fromClipFile);
    double fromStartTime = fromActualDuration - crossfadeDuration;
    
    if (logCallback) {
        logCallback("  - From clip actual duration: " + juce::String(fromActualDuration) + "s");
        logCallback("  - From start time calculation: " + juce::String(fromActualDuration) + " - " + juce::String(crossfadeDuration) + " = " + juce::String(fromStartTime) + "s");
    }
    
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    juce::String extractFromCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + fromClipFile.getFullPathName() + "\"" +
        " -ss " + juce::String(fromStartTime) +
        " -t " + juce::String(crossfadeDuration) +
        " " + losslessParams +  // Use lossless encoding for intermediate files
        " -an \"" + fromXOut.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractFromCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract crossfade-out segment");
        return false;
    }
    
    // 2. Extract crossfade-in segment from toClip (first n seconds)
    juce::File toXIn = tempDirectory.getChildFile(type + "_" + juce::String(toIndex) + "_x_in.mp4");
    juce::String extractToCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + toClipFile.getFullPathName() + "\"" +
        " -t " + juce::String(crossfadeDuration) +
        " " + losslessParams +  // Use lossless encoding for intermediate files
        " -an \"" + toXIn.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractToCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract crossfade-in segment");
        return false;
    }
    
    // 3. Create crossfade transition between the segments (FIXED XFADE with compatibility check)
    juce::File crossfadeFile = tempDirectory.getChildFile(type + "_" + juce::String(fromIndex) + "_to_" + juce::String(toIndex) + "_x.mp4");
    
    // FIRST ATTEMPT: Try direct crossfade
    juce::String crossfadeCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + fromXOut.getFullPathName() + "\"" +
        " -i \"" + toXIn.getFullPathName() + "\"" +
        " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) + 
        ":offset=0[v]\"" +
        " -map \"[v]\"" +
        " -t " + juce::String(crossfadeDuration) +
        " -pix_fmt yuv420p" +  // Standard pixel format for compatibility
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + crossfadeFile.getFullPathName() + "\"";
    
    if (logCallback) logCallback("Attempting crossfade with command: " + crossfadeCommand.substring(0, 150) + "...");
    
    if (!ffmpegExecutor->executeCommand(crossfadeCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("CROSSFADE FAILED - attempting format normalization and retry...");
        
        // FALLBACK: Normalize both inputs to exact same format and try again
        juce::File normalizedFromXOut = tempDirectory.getChildFile(type + "_" + juce::String(fromIndex) + "_x_out_normalized.mp4");
        juce::File normalizedToXIn = tempDirectory.getChildFile(type + "_" + juce::String(toIndex) + "_x_in_normalized.mp4");

        // Capture source stream details so normalization preserves their native characteristics.
        const auto fromInfo = ffmpegExecutor->getVideoStreamInfo(fromClipFile);
        const auto toInfo = ffmpegExecutor->getVideoStreamInfo(toClipFile);

        int targetWidth = fromInfo.width > 0 ? fromInfo.width : toInfo.width;
        int targetHeight = fromInfo.height > 0 ? fromInfo.height : toInfo.height;
        double targetFps = fromInfo.fps > 0.01 ? fromInfo.fps : toInfo.fps;

        if (targetWidth <= 0 || targetHeight <= 0) {
            targetWidth = 1920;
            targetHeight = 1080;
            if (logCallback) logCallback("WARNING: Unable to probe clip resolution — falling back to 1920x1080 for normalization");
        }

        if (targetFps <= 0.01) {
            targetFps = 30.0;
            if (logCallback) logCallback("WARNING: Unable to probe clip FPS — falling back to 30fps for normalization");
        }

        juce::String fpsString = juce::String(targetFps, 3);
        juce::String sizeString = juce::String(targetWidth) + "x" + juce::String(targetHeight);
        juce::String normalizationParams = " " + losslessParams +
                                           " -pix_fmt yuv420p" +
                                           " -r " + fpsString +
                                           " -s " + sizeString +
                                           " -vsync cfr";

        if (logCallback) {
            logCallback("Normalizing crossfade inputs to " + sizeString + " @" + fpsString + "fps using lossless settings");
        }

        // Normalize first input with very specific format parameters
        juce::String normalizeFrom = ffmpegExecutor->getFFmpegPath() +
            " -y -i \"" + fromXOut.getFullPathName() + "\"" +
            normalizationParams +
            " -an \"" + normalizedFromXOut.getFullPathName() + "\"";

        juce::String normalizeTo = ffmpegExecutor->getFFmpegPath() +
            " -y -i \"" + toXIn.getFullPathName() + "\"" +
            normalizationParams +
            " -an \"" + normalizedToXIn.getFullPathName() + "\"";

        if (ffmpegExecutor->executeCommand(normalizeFrom, 0.0, 1.0) &&
            ffmpegExecutor->executeCommand(normalizeTo, 0.0, 1.0)) {

            // Try crossfade again with normalized inputs
            juce::String normalizedCrossfadeCommand = ffmpegExecutor->getFFmpegPath() +
                " -y -i \"" + normalizedFromXOut.getFullPathName() + "\"" +
                " -i \"" + normalizedToXIn.getFullPathName() + "\"" +
                " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) +
                ":offset=0[v]\"" +
                " -map \"[v]\"" +
                " -t " + juce::String(crossfadeDuration) +
                " -pix_fmt yuv420p " + losslessParams +
                " -an \"" + crossfadeFile.getFullPathName() + "\"";
            
            if (logCallback) logCallback("Retrying crossfade with normalized inputs...");
            
            if (!ffmpegExecutor->executeCommand(normalizedCrossfadeCommand, 0.0, 1.0)) {
                if (logCallback) logCallback("ERROR: Crossfade failed even after normalization - creating simple fade as fallback");
                
                // ULTIMATE FALLBACK: Create a simple black fade instead of crossfade
                juce::String fadeCommand = ffmpegExecutor->getFFmpegPath() +
                    " -y -i \"" + fromXOut.getFullPathName() + "\"" +
                    " -filter_complex \"[0:v]fade=out:st=0:d=" + juce::String(crossfadeDuration * 0.5) + 
                    ",fade=in:st=" + juce::String(crossfadeDuration * 0.5) + ":d=" + juce::String(crossfadeDuration * 0.5) + "[v]\"" +
                    " -map \"[v]\"" +
                    " -t " + juce::String(crossfadeDuration) +
                    " -pix_fmt yuv420p -c:v libx264 -preset ultrafast -crf 17" +
                    " -an \"" + crossfadeFile.getFullPathName() + "\"";
                
                if (!ffmpegExecutor->executeCommand(fadeCommand, 0.0, 1.0)) {
                    if (logCallback) logCallback("ERROR: All crossfade attempts failed");
                    return false;
                } else {
                    if (logCallback) logCallback("Created fallback fade transition");
                }
            } else {
                if (logCallback) logCallback("Crossfade succeeded with normalized inputs");
            }
            
            // Clean up normalized files
            normalizedFromXOut.deleteFile();
            normalizedToXIn.deleteFile();
        } else {
            if (logCallback) logCallback("ERROR: Failed to normalize inputs for crossfade");
            return false;
        }
    } else {
        if (logCallback) logCallback("Crossfade succeeded on first attempt");
    }
    
    // 4. Create body segments (clips with crossfade portions removed)
    // From clip: remove last n seconds to create body_cut_out
    juce::File fromBodyCutOut = tempDirectory.getChildFile(type + "_" + juce::String(fromIndex) + "_body_cut_out.mp4");
    double fromBodyDuration = fromActualDuration - crossfadeDuration;
    
    if (logCallback)
        logCallback("  - From body duration after trim: " + juce::String(fromBodyDuration) + "s");
    
    if (fromBodyDuration <= 0.001)
    {
        if (logCallback) logCallback("WARNING: from-body duration too small (" + juce::String(fromBodyDuration) + "s); clamping to 0.1s");
        fromBodyDuration = 0.1;
    }
    
    if (!executeTrimWithFallback(fromClipFile,
                                 fromBodyCutOut,
                                 0.0,
                                 fromBodyDuration,
                                 "creating from-body segment for " + type + "_" + juce::String(fromIndex)))
        return false;
    
    // To clip: remove first n seconds to create body_cut_in
    juce::File toBodyCutIn = tempDirectory.getChildFile(type + "_" + juce::String(toIndex) + "_body_cut_in.mp4");
    double toActualDuration = ffmpegExecutor->getFileDuration(toClipFile);
    double toBodyDuration = toActualDuration - crossfadeDuration;
    
    if (logCallback)
    {
        logCallback("  - To clip actual duration: " + juce::String(toActualDuration) + "s");
        logCallback("  - To body duration after trim: " + juce::String(toBodyDuration) + "s");
    }
    
    if (toBodyDuration <= 0.001)
    {
        if (logCallback) logCallback("WARNING: to-body duration too small (" + juce::String(toBodyDuration) + "s); clamping to 0.1s");
        toBodyDuration = 0.1;
    }
    
    if (!executeTrimWithFallback(toClipFile,
                                 toBodyCutIn,
                                 crossfadeDuration,
                                 toBodyDuration,
                                 "creating to-body segment for " + type + "_" + juce::String(toIndex)))
        return false;

    if (logCallback) logCallback("Created crossfade: " + crossfadeFile.getFileName() + " (" + juce::String(crossfadeDuration) + "s)");
    return true;
}

bool TimelineAssembler::createMiddleClipBodySegment(const juce::String& type, size_t clipIndex, 
                                                   double prevCrossfadeDuration, double nextCrossfadeDuration, 
                                                   const juce::File& tempDirectory)
{
    if (logCallback) logCallback("Creating middle clip body segment for " + type + "_" + juce::String(clipIndex));
    
    juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(clipIndex) + ".mp4");
    if (!clipFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Source clip not found for middle body segment");
        return false;
    }
    
    double actualDuration = ffmpegExecutor->getFileDuration(clipFile);
    double bodyDuration = actualDuration - prevCrossfadeDuration - nextCrossfadeDuration;
    
    if (bodyDuration <= 0.1) {
        if (logCallback) logCallback("WARNING: Middle clip body duration too short: " + juce::String(bodyDuration) + "s");
        bodyDuration = 0.1; // Minimum duration
    }
    
    juce::File outputFile = tempDirectory.getChildFile(type + "_" + juce::String(clipIndex) + "_body_cut_in_cut_out.mp4");
    juce::String description = "creating middle clip body segment for " + type + "_" + juce::String(clipIndex);
    if (!executeTrimWithFallback(clipFile,
                                 outputFile,
                                 prevCrossfadeDuration,
                                 bodyDuration,
                                 description))
    {
        if (logCallback) logCallback("ERROR: Failed to create middle clip body segment");
        return false;
    }

    if (logCallback) logCallback("Created middle body segment: " + outputFile.getFileName() + " (" + juce::String(bodyDuration) + "s)");
    return true;
}

bool TimelineAssembler::generateIntroToLoopCrossfade(const RenderTypes::VideoClipInfo& lastIntroClip, const RenderTypes::VideoClipInfo& firstLoopClip,
                                                    const juce::File& tempDirectory)
{
    double crossfadeDuration = lastIntroClip.crossfade;
    if (crossfadeDuration <= 0.001) {
        return true; // No crossfade needed
    }
    
    // Get source conformed clips
    // Find the last intro clip index
    size_t lastIntroIndex = 0;
    for (size_t i = 10; i > 0; i--) { // Check up to 10 clips
        juce::File testFile = tempDirectory.getChildFile("intro_" + juce::String(i-1) + ".mp4");
        if (testFile.existsAsFile()) {
            lastIntroIndex = i-1;
            break;
        }
    }
    
    juce::File lastIntroFile = tempDirectory.getChildFile("intro_" + juce::String(lastIntroIndex) + ".mp4");
    juce::File firstLoopFile = tempDirectory.getChildFile("loop_0.mp4");
    
    if (!lastIntroFile.existsAsFile() || !firstLoopFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Source conformed clips not found for intro-to-loop crossfade");
        if (logCallback) logCallback("  Last intro clip (" + lastIntroFile.getFileName() + ") exists: " + (lastIntroFile.existsAsFile() ? "yes" : "no"));
        if (logCallback) logCallback("  First loop clip (" + firstLoopFile.getFileName() + ") exists: " + (firstLoopFile.existsAsFile() ? "yes" : "no"));
        return false;
    }
    
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    
    // Extract last n seconds from last intro clip for crossfade-out
    juce::File introXOut = tempDirectory.getChildFile("intro_to_loop_x_out.mp4");
    double introDuration = ffmpegExecutor->getFileDuration(lastIntroFile);
    double introStartTime = introDuration - crossfadeDuration;
    
    juce::String extractIntroCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + lastIntroFile.getFullPathName() + "\"" +
        " -ss " + juce::String(introStartTime) +
        " -t " + juce::String(crossfadeDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + introXOut.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractIntroCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract intro crossfade-out segment");
        return false;
    }
    
    // Extract first n seconds from first loop clip for crossfade-in
    juce::File loopXIn = tempDirectory.getChildFile("intro_to_loop_x_in.mp4");
    juce::String extractLoopCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + firstLoopFile.getFullPathName() + "\"" +
        " -t " + juce::String(crossfadeDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + loopXIn.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractLoopCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract loop crossfade-in segment");
        return false;
    }
    
    // Create the crossfade transition (FIXED XFADE)
    juce::File crossfadeFile = tempDirectory.getChildFile("intro_to_loop_x.mp4");
    juce::String crossfadeCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + introXOut.getFullPathName() + "\"" +
        " -i \"" + loopXIn.getFullPathName() + "\"" +
        " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) + 
        ":offset=0[v]\"" +
        " -map \"[v]\"" +
        " -t " + juce::String(crossfadeDuration) +
        " -pix_fmt yuv420p" +  // Standard pixel format for compatibility
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + crossfadeFile.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(crossfadeCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to create intro-to-loop crossfade");
        return false;
    }
    
    if (logCallback) logCallback("Created intro-to-loop crossfade: " + crossfadeFile.getFileName() + " (" + juce::String(crossfadeDuration) + "s)");
    return true;
}

bool TimelineAssembler::generateLoopToLoopCrossfade(const RenderTypes::VideoClipInfo& lastLoopClip, const RenderTypes::VideoClipInfo& firstLoopClip,
                                                   const juce::File& tempDirectory)
{
    double crossfadeDuration = lastLoopClip.crossfade;
    if (crossfadeDuration <= 0.001) {
        return true; // No crossfade needed
    }
    
    // Get source conformed clips
    size_t lastLoopIndex = 0;
    // Find the last loop clip index by checking which files exist
    for (size_t i = 10; i > 0; i--) { // Check up to 10 clips
        juce::File testFile = tempDirectory.getChildFile("loop_" + juce::String(i-1) + ".mp4");
        if (testFile.existsAsFile()) {
            lastLoopIndex = i-1;
            break;
        }
    }
    
    juce::File lastLoopFile = tempDirectory.getChildFile("loop_" + juce::String(lastLoopIndex) + ".mp4");
    juce::File firstLoopFile = tempDirectory.getChildFile("loop_0.mp4");
    
    if (!lastLoopFile.existsAsFile() || !firstLoopFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Source files not found for loop-to-loop crossfade");
        return false;
    }
    
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    
    // Extract last n seconds from last loop clip for crossfade-out
    juce::File loopXOut = tempDirectory.getChildFile("loop_to_loop_x_out.mp4");
    double lastLoopDuration = ffmpegExecutor->getFileDuration(lastLoopFile);
    double lastLoopStartTime = lastLoopDuration - crossfadeDuration;
    
    juce::String extractLastCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + lastLoopFile.getFullPathName() + "\"" +
        " -ss " + juce::String(lastLoopStartTime) +
        " -t " + juce::String(crossfadeDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + loopXOut.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractLastCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract last loop crossfade-out segment");
        return false;
    }
    
    // Extract first n seconds from first loop clip for crossfade-in
    juce::File loopXIn = tempDirectory.getChildFile("loop_to_loop_x_in.mp4");
    juce::String extractFirstCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + firstLoopFile.getFullPathName() + "\"" +
        " -t " + juce::String(crossfadeDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + loopXIn.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(extractFirstCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract first loop crossfade-in segment");
        return false;
    }
    
    // Create the crossfade transition (FIXED XFADE)
    juce::File crossfadeFile = tempDirectory.getChildFile("loop_to_loop_x.mp4");
    juce::String crossfadeCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + loopXOut.getFullPathName() + "\"" +
        " -i \"" + loopXIn.getFullPathName() + "\"" +
        " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) + 
        ":offset=0[v]\"" +
        " -map \"[v]\"" +
        " -t " + juce::String(crossfadeDuration) +
        " -pix_fmt yuv420p" +  // Standard pixel format for compatibility
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + crossfadeFile.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(crossfadeCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to create loop-to-loop crossfade");
        return false;
    }
    
    if (logCallback) logCallback("Created loop-to-loop crossfade: " + crossfadeFile.getFileName() + " (" + juce::String(crossfadeDuration) + "s)");
    return true;
}

bool TimelineAssembler::buildRawSequence(const juce::String& type, const std::vector<RenderTypes::VideoClipInfo>& clips,
                                       const juce::File& tempDirectory, const juce::File& outputFile)
{
    // Calculate expected total duration
    double expectedDuration = 0.0;
    for (const auto& clip : clips) {
        expectedDuration += clip.duration;
    }
    
    if (logCallback) {
        logCallback("Building raw " + type + " sequence");
        logCallback("  - Number of clips: " + juce::String(clips.size()));
        logCallback("  - Expected total duration: " + juce::String(expectedDuration) + "s");
    }
    
    if (clips.size() == 1) {
        // Simple case: single clip, just copy the conformed clip
        juce::File sourceFile = tempDirectory.getChildFile(type + "_0.mp4");
        if (!sourceFile.existsAsFile()) {
            if (logCallback) logCallback("ERROR: Source file not found: " + sourceFile.getFileName());
            return false;
        }
        
        if (!sourceFile.copyFileTo(outputFile)) {
            if (logCallback) logCallback("ERROR: Failed to copy " + sourceFile.getFileName() + " to " + outputFile.getFileName());
            return false;
        }
        
        if (logCallback) logCallback("Single clip raw sequence created: " + outputFile.getFileName());
        return true;
    }
    
    // Multiple clips case - use crossfade components according to algorithm.md
    if (logCallback) logCallback("Creating concatenation with crossfades for " + juce::String(clips.size()) + " clips");
    
    // Create concat file (delete existing file first to prevent appending)
    juce::File concatFile = tempDirectory.getChildFile(type + "_concat.txt");
    if (concatFile.existsAsFile()) {
        concatFile.deleteFile();
    }
    juce::FileOutputStream concatStream(concatFile);
    if (!concatStream.openedOk()) {
        if (logCallback) logCallback("ERROR: Failed to create concat file");
        return false;
    }
    
    // Build sequence using crossfade components: 
    // clip_0_body_cut_out + clip_0_to_clip_1_x + clip_1_body_cut_in_cut_out + clip_1_to_clip_2_x + clip_2_cut_in...
    if (logCallback) logCallback("Building " + type + " sequence from " + juce::String(clips.size()) + " clips:");
    
    for (size_t i = 0; i < clips.size(); i++) {
        if (i == 0) {
            // First clip: use body_cut_out if crossfade exists, otherwise use full clip
            if (clips[i].crossfade > 0.001) {
                juce::File bodyFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + "_body_cut_out.mp4");
                if (bodyFile.existsAsFile()) {
                    double fileDuration = ffmpegExecutor->getFileDuration(bodyFile);
                    if (logCallback) logCallback("  Adding: " + bodyFile.getFileName() + " (duration: " + juce::String(fileDuration) + "s)");
                    concatStream.writeText("file '" + bodyFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                } else {
                    // Fallback to original clip if body segment not found
                    juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                    double fileDuration = ffmpegExecutor->getFileDuration(clipFile);
                    if (logCallback) logCallback("  Adding (fallback): " + clipFile.getFileName() + " (duration: " + juce::String(fileDuration) + "s)");
                    concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                }
            } else {
                // No crossfade, use full clip
                juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                if (logCallback) logCallback("  Adding (no crossfade): " + clipFile.getFileName());
                concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
            }
            
        } else if (i == clips.size() - 1) {
            // Last clip: add crossfade transition first, then body_cut_in
            if (clips[i-1].crossfade > 0.001) {
                juce::File crossfadeFile = tempDirectory.getChildFile(type + "_" + juce::String(i-1) + "_to_" + juce::String(i) + "_x.mp4");
                if (crossfadeFile.existsAsFile()) {
                    if (logCallback) logCallback("  Adding crossfade: " + crossfadeFile.getFileName());
                    concatStream.writeText("file '" + crossfadeFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                }
            }
            
            // Add the remaining part of last clip (after crossfade removal)
            if (clips[i-1].crossfade > 0.001) {
                juce::File bodyFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + "_body_cut_in.mp4");
                if (bodyFile.existsAsFile()) {
                    if (logCallback) logCallback("  Adding last body: " + bodyFile.getFileName());
                    concatStream.writeText("file '" + bodyFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                } else {
                    // Fallback to original clip
                    juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                    if (logCallback) logCallback("  Adding last fallback: " + clipFile.getFileName());
                    concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                }
            } else {
                // No crossfade, use full clip
                juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                if (logCallback) logCallback("  Adding last full clip: " + clipFile.getFileName());
                concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
            }
            
        } else {
            // Middle clips: add crossfade transition first, then body_cut_in_cut_out
            if (clips[i-1].crossfade > 0.001) {
                juce::File crossfadeFile = tempDirectory.getChildFile(type + "_" + juce::String(i-1) + "_to_" + juce::String(i) + "_x.mp4");
                if (crossfadeFile.existsAsFile()) {
                    double fileDuration = ffmpegExecutor->getFileDuration(crossfadeFile);
                    if (logCallback) logCallback("  Adding crossfade: " + crossfadeFile.getFileName() + " (duration: " + juce::String(fileDuration) + "s)");
                    concatStream.writeText("file '" + crossfadeFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                }
            }
            
            // For middle clips, use body_cut_in_cut_out (both ends trimmed) if available
            juce::File bodyFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + "_body_cut_in_cut_out.mp4");
            if (bodyFile.existsAsFile()) {
                // Perfect! Use the properly trimmed middle segment
                if (logCallback) logCallback("  Adding middle body: " + bodyFile.getFileName());
                concatStream.writeText("file '" + bodyFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
            } else if (clips[i-1].crossfade > 0.001) {
                // Fallback: use body_cut_in (only beginning trimmed)
                juce::File fallbackFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + "_body_cut_in.mp4");
                if (fallbackFile.existsAsFile()) {
                    if (logCallback) logCallback("  Adding body fallback: " + fallbackFile.getFileName());
                    concatStream.writeText("file '" + fallbackFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                } else {
                    // Last resort: use original clip
                    juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                    if (logCallback) logCallback("  Adding original (last resort): " + clipFile.getFileName());
                    concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
                }
            } else {
                // No crossfade, use full clip
                juce::File clipFile = tempDirectory.getChildFile(type + "_" + juce::String(i) + ".mp4");
                if (logCallback) logCallback("  Adding full clip (no crossfade): " + clipFile.getFileName());
                concatStream.writeText("file '" + clipFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
            }
        }
    }
    concatStream.flush();
    
    // Execute concat command
    if (!executeConcatWithFallback(concatFile, outputFile, "concatenating loop clips", 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to concatenate clips");
        return false;
    }
    
    if (logCallback) logCallback("Multi-clip raw sequence created: " + outputFile.getFileName());
    return true;
}

bool TimelineAssembler::trimFinalCrossfade(const juce::File& inputFile, const juce::File& outputFile,
                                         const juce::File& crossfadeOutputFile, double crossfadeDuration)
{
    if (logCallback) logCallback("Trimming final crossfade of " + juce::String(crossfadeDuration) + "s");
    
    // Get the duration of the input file
    double inputDuration = ffmpegExecutor->getFileDuration(inputFile);
    if (inputDuration <= 0) {
        if (logCallback) logCallback("ERROR: Could not get duration of input file");
        return false;
    }
    
    double trimmedDuration = inputDuration - crossfadeDuration;
    if (trimmedDuration <= 0) {
        if (logCallback) logCallback("ERROR: Crossfade duration exceeds input duration");
        return false;
    }
    
    // Create the main sequence (trimmed)
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    juce::String trimCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + inputFile.getFullPathName() + "\"" +
        " -t " + juce::String(trimmedDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + outputFile.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(trimCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to create trimmed sequence");
        return false;
    }
    
    // Extract and save the crossfade part (last n seconds) to crossfadeOutputFile
    double crossfadeStartTime = inputDuration - crossfadeDuration;
    juce::String crossfadeCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + inputFile.getFullPathName() + "\"" +
        " -ss " + juce::String(crossfadeStartTime) +
        " -t " + juce::String(crossfadeDuration) +
        " " + encodingParams +  // Lossless encoding already includes codec
        " -an \"" + crossfadeOutputFile.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(crossfadeCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to extract crossfade segment");
        return false;
    }
    
    if (logCallback) logCallback("Created trimmed sequence: " + juce::String(trimmedDuration) + "s");
    if (logCallback) logCallback("Saved crossfade segment: " + crossfadeOutputFile.getFileName() + " (" + juce::String(crossfadeDuration) + "s)");
    return true;
}

bool TimelineAssembler::copyFile(const juce::File& sourceFile, const juce::File& destFile)
{
    return sourceFile.copyFileTo(destFile);
}

bool TimelineAssembler::extractLoopVariants(const juce::File& loopSequenceRaw, const juce::File& tempDirectory, const juce::String& variantType,
                                           const std::vector<RenderTypes::VideoClipInfo>& introClips, const std::vector<RenderTypes::VideoClipInfo>& loopClips)
{
    if (logCallback) logCallback("Extracting " + variantType + " loop variants");
    
    if (!loopSequenceRaw.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Loop sequence raw file not found");
        return false;
    }
    
    double rawDuration = ffmpegExecutor->getFileDuration(loopSequenceRaw);
    if (rawDuration <= 0) {
        if (logCallback) logCallback("ERROR: Could not get duration of loop sequence raw");
        return false;
    }
    
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    
    if (variantType == "intro_based") {
        // For Intro-Based: Remove first crossfade segment (matching last intro clip's crossfade duration)
        double introCrossfadeDuration = introClips.empty() ? 0.0 : introClips.back().crossfade;
        
        // SAFETY CHECK: Only use crossfade if we have both intro AND loop clips
        bool hasIntroToLoopTransition = !introClips.empty() && !loopClips.empty();
        double actualIntroCrossfade = hasIntroToLoopTransition ? introCrossfadeDuration : 0.0;
        double actualLoopCrossfade = (loopClips.size() > 1) ? (loopClips.empty() ? 0.0 : loopClips.back().crossfade) : 0.0;
        
        // Extract loop_from_intro_sequence_x_in (first n seconds)
        juce::File loopFromIntroXIn = tempDirectory.getChildFile("loop_from_intro_sequence_x_in.mp4");
        juce::String extractXInCommand = ffmpegExecutor->getFFmpegPath() +
            " -y -i \"" + loopSequenceRaw.getFullPathName() + "\"" +
            " -t " + juce::String(actualIntroCrossfade) +
            " " + encodingParams +  // Lossless encoding already includes codec
            " -an \"" + loopFromIntroXIn.getFullPathName() + "\"";
        
        if (!ffmpegExecutor->executeCommand(extractXInCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Failed to extract loop_from_intro_sequence_x_in");
            return false;
        }
        
        // Extract loop_from_intro_body_cut_in_cut_out (remaining part after removing first n seconds AND last loop crossfade seconds)
        juce::File loopFromIntroBody = tempDirectory.getChildFile("loop_from_intro_body_cut_in_cut_out.mp4");
        
        double bodyDuration = rawDuration - actualIntroCrossfade - actualLoopCrossfade;  // Cut off BOTH ends!
        
        // Additional safety check for negative or zero durations
        if (bodyDuration <= 0.1) {
            if (logCallback) logCallback("Single clip detected - using full sequence as body (no crossfade extraction needed)");
            // For single clips, just copy the full sequence as the body
            if (!copyFile(loopSequenceRaw, loopFromIntroBody)) {
                if (logCallback) logCallback("ERROR: Failed to copy loop sequence as body");
                return false;
            }
        } else {
            juce::String extractBodyCommand = ffmpegExecutor->getFFmpegPath() +
                " -y -i \"" + loopSequenceRaw.getFullPathName() + "\"" +
                " -ss " + juce::String(actualIntroCrossfade) +
                " -t " + juce::String(bodyDuration) +
                " " + encodingParams +  // Lossless encoding already includes codec
                " -an \"" + loopFromIntroBody.getFullPathName() + "\"";
            
            if (!ffmpegExecutor->executeCommand(extractBodyCommand, 0.0, 1.0)) {
                if (logCallback) logCallback("ERROR: Failed to extract loop_from_intro_body");
                return false;
            }
        }
        
    } else if (variantType == "loop_based") {
        double loopCrossfadeDuration = loopClips.empty() ? 0.0 : loopClips.back().crossfade;

        // Extract X_OUT (last N seconds of loop sequence for fade out)
        juce::File loopFromLoopXOut = tempDirectory.getChildFile("loop_from_loop_sequence_x_out.mp4");
        double xOutStartTime = rawDuration - loopCrossfadeDuration;
        juce::String extractXOutCommand = ffmpegExecutor->getFFmpegPath() +
            " -y" +                                                    // Overwrite output file
            " -i \"" + loopSequenceRaw.getFullPathName() + "\"" +      // Input: concatenated loop sequence
            " -ss " + juce::String(xOutStartTime) +                    // Seek to start position
            " -t " + juce::String(loopCrossfadeDuration) +             // Extract for crossfade duration
            " " + losslessParams +                                     // Use lossless encoding for intermediate files
            " -an" +                                                   // No audio
            " \"" + loopFromLoopXOut.getFullPathName() + "\"";         // Output: fade out source
        
        if (!ffmpegExecutor->executeCommand(extractXOutCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Failed to extract X_OUT for loop-to-loop crossfade");
            return false;
        }
        
        // Extract X_IN (first N seconds for fade in)
        juce::File loopFromLoopXIn = tempDirectory.getChildFile("loop_from_loop_sequence_x_in.mp4");
        
        // FFmpeg command: Extract first N seconds of loop_sequence_raw.mp4
        juce::String extractXInCommand = ffmpegExecutor->getFFmpegPath() +
            " -y" +                                                    // Overwrite output file
            " -i \"" + loopSequenceRaw.getFullPathName() + "\"" +      // Input: concatenated loop sequence (same as X_OUT!)
            " -t " + juce::String(loopCrossfadeDuration) +             // Extract for crossfade duration from start
            " " + losslessParams +                                     // Use lossless encoding for intermediate files
            " -an" +                                                   // No audio
            " \"" + loopFromLoopXIn.getFullPathName() + "\"";          // Output: fade in destination
        
        if (!ffmpegExecutor->executeCommand(extractXInCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Failed to extract X_IN for loop-to-loop crossfade");
            return false;
        }
        
        // STEP 3: Extract BODY - the middle part with crossfade portions removed from both ends
        // This ensures no content duplication when crossfade is prepended to body
        juce::File loopFromLoopBody = tempDirectory.getChildFile("loop_from_loop_body_cut_in_cut_out.mp4");
        double bodyDuration = rawDuration - loopCrossfadeDuration; // Only remove start, keep full end for overlap
        if (bodyDuration < 0.1) {
            if (logCallback) logCallback("WARNING: Body duration too short after removing crossfades from both ends");
            bodyDuration = 0.1; // Minimum duration to prevent errors
        }
        
        // Extract body with start cut off, but keep full end
        juce::String extractBodyCommand = ffmpegExecutor->getFFmpegPath() +
            " -y" +                                                    // Overwrite output file
            " -i \"" + loopSequenceRaw.getFullPathName() + "\"" +      // Input: concatenated loop sequence (same as X_OUT and X_IN!)
            " -ss " + juce::String(loopCrossfadeDuration) +            // Skip first N seconds (will be replaced by crossfade)
            " -t " + juce::String(bodyDuration) +                      // Extract middle portion only
            " " + losslessParams +                                     // Use lossless encoding for intermediate files
            " -an" +                                                   // No audio  
            " \"" + loopFromLoopBody.getFullPathName() + "\"";         // Output: body with ends cut off
        
        if (!ffmpegExecutor->executeCommand(extractBodyCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Failed to extract BODY for loop-to-loop sequence");
            return false;
        }
    }
    
    if (logCallback) logCallback("Successfully extracted " + variantType + " loop variants");
    return true;
}

bool TimelineAssembler::generateInterpolatedCrossfades(const juce::File& tempDirectory,
                                                     const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                                     const std::vector<RenderTypes::VideoClipInfo>& loopClips)
{
    if (logCallback) logCallback("Generating interpolated crossfades with exact durations");
    
    // Use lossless for all intermediate encoding
    juce::String encodingParams = losslessParams;
    
    // Generate loop_from_intro_sequence_x crossfade
    juce::File introXOut = tempDirectory.getChildFile("loop_from_intro_sequence_x_out.mp4");
    juce::File introXIn = tempDirectory.getChildFile("loop_from_intro_sequence_x_in.mp4");
    juce::File introLoopCrossfade = tempDirectory.getChildFile("loop_from_intro_sequence_x.mp4");
    
    if (introXOut.existsAsFile() && introXIn.existsAsFile()) {
        // Use EXACT crossfade duration from intro clip data (intro-to-loop crossfade)
        double crossfadeDuration = introClips.empty() ? 0.0 : introClips.back().crossfade;
        
        // Skip crossfade creation if no intro-to-loop transition needed
        if (introClips.empty() || loopClips.empty()) {
            if (logCallback) logCallback("Single intro clip detected - skipping intro-to-loop crossfade (not needed)");
        } else {
            if (logCallback) {
                logCallback("Creating intro-to-loop crossfade with EXACT duration: " + juce::String(crossfadeDuration) + "s");
            }
            
            juce::String introLoopCommand = ffmpegExecutor->getFFmpegPath() +
                " -y -i \"" + introXOut.getFullPathName() + "\"" +
                " -i \"" + introXIn.getFullPathName() + "\"" +
                " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) + 
                ":offset=0[v]\"" +
                " -map \"[v]\"" +
                " -t " + juce::String(crossfadeDuration) +
                " " + encodingParams +  // Lossless encoding already includes codec
                " -an \"" + introLoopCrossfade.getFullPathName() + "\"";
            
            if (!ffmpegExecutor->executeCommand(introLoopCommand, 0.0, 1.0)) {
                if (logCallback) logCallback("ERROR: Failed to create intro-to-loop crossfade");
                return false;
            }
            if (logCallback) logCallback("Created intro-to-loop crossfade: " + introLoopCrossfade.getFileName());
        }
    }
    
    // Loop-to-loop crossfade: transition from end of last loop to beginning of first loop
    juce::File loopXOut = tempDirectory.getChildFile("loop_from_loop_sequence_x_out.mp4");
    juce::File loopXIn = tempDirectory.getChildFile("loop_from_loop_sequence_x_in.mp4");
    juce::File loopLoopCrossfade = tempDirectory.getChildFile("loop_from_loop_sequence_x.mp4");

    if (loopXOut.existsAsFile() && loopXIn.existsAsFile()) {
        double crossfadeDuration = loopClips.empty() ? 0.0 : loopClips.back().crossfade;

        if (crossfadeDuration <= 0.001 || loopClips.size() <= 1) {
            // No crossfade needed
        } else {
        juce::String loopLoopCommand = ffmpegExecutor->getFFmpegPath() +
            " -y" +                                                    // Overwrite output without prompting
            " -i \"" + loopXOut.getFullPathName() + "\"" +            // Input 0: outgoing loop frames
            " -i \"" + loopXIn.getFullPathName() + "\"" +             // Input 1: incoming loop frames
            " -filter_complex \"[0:v][1:v]xfade=transition=fade:duration=" + juce::String(crossfadeDuration) + 
            ":offset=0[v]\"" +                                         // xfade: fade transition starting at offset 0
            " -map \"[v]\"" +                                          // Map the crossfaded video output
            " -t " + juce::String(crossfadeDuration) +                 // Limit output duration to crossfade length
            " " + encodingParams +  // Lossless encoding already includes codec // Video encoding
            " -an \"" + loopLoopCrossfade.getFullPathName() + "\"";   // No audio (-an), output to crossfade file
        
        // Execute the FFmpeg command to generate the crossfade
        if (!ffmpegExecutor->executeCommand(loopLoopCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Failed to create loop-to-loop crossfade");
            return false;
        }
        if (logCallback) logCallback("Created loop-to-loop crossfade: " + loopLoopCrossfade.getFileName());
        }  // Close the else block
    }
    
    if (logCallback) logCallback("Successfully generated interpolated crossfades");
    return true;
}

bool TimelineAssembler::concatenateTwoFiles(const juce::File& file1, const juce::File& file2, const juce::File& outputFile)
{
    if (logCallback) logCallback("Concatenating " + file1.getFileName() + " + " + file2.getFileName());
    
    if (!file1.existsAsFile() || !file2.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Input files missing for concatenation");
        return false;
    }

    juce::String uniqueName = "concat_" + outputFile.getFileNameWithoutExtension() + "_" + juce::String(juce::Time::currentTimeMillis()) + ".txt";
    juce::File concatFile = outputFile.getParentDirectory().getChildFile(uniqueName);
    juce::FileOutputStream concatStream(concatFile);
    if (!concatStream.openedOk()) {
        if (logCallback) logCallback("ERROR: Failed to create concat file for two files");
        return false;
    }
    
    concatStream.writeText("file '" + file1.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
    concatStream.writeText("file '" + file2.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
    concatStream.flush();

    if (!executeConcatWithFallback(concatFile, outputFile, "concatenating two files", 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Concat demuxer failed for two-file merge, attempting filter-based fallback");

        if (outputFile.existsAsFile())
            outputFile.deleteFile();

        juce::String filterCommand = ffmpegExecutor->getFFmpegPath() +
            " -y" +
            " -i \"" + file1.getFullPathName() + "\"" +
            " -i \"" + file2.getFullPathName() + "\"" +
            " -filter_complex \"[0:v][1:v]concat=n=2:v=1:a=0[vout]\"" +
            " -map \"[vout]\"" +
            " " + losslessParams +
            " -an \"" + outputFile.getFullPathName() + "\"";

        if (!ffmpegExecutor->executeCommand(filterCommand, 0.0, 1.0)) {
            if (logCallback) logCallback("ERROR: Filter-based concat fallback also failed");
            concatFile.deleteFile();
            return false;
        }

        if (logCallback) logCallback("Filter-based concat fallback succeeded");
    }
    
    // Clean up the temporary concat list file
    concatFile.deleteFile();
    
    if (logCallback) logCallback("Successfully concatenated: " + outputFile.getFileName());
    return true;
}

bool TimelineAssembler::concatenateWithCrossfade(const juce::File& crossfadeFile, const juce::File& sequenceFile, const juce::File& outputFile)
{
    if (logCallback) logCallback("Concatenating crossfade + sequence: " + crossfadeFile.getFileName() + " + " + sequenceFile.getFileName());
    
    if (!crossfadeFile.existsAsFile() || !sequenceFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Input files don't exist for crossfade concatenation");
        return false;
    }
    
    // Create concat file for FFmpeg
    juce::File concatFile = outputFile.getParentDirectory().getChildFile("crossfade_concat.txt");
    juce::FileOutputStream concatStream(concatFile);
    if (!concatStream.openedOk()) {
        if (logCallback) logCallback("ERROR: Failed to create concat file for crossfade");
        return false;
    }
    
    // Add crossfade first, then sequence
    concatStream.writeText("file '" + crossfadeFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
    concatStream.writeText("file '" + sequenceFile.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
    concatStream.flush();
    
    // Execute concat command
    if (!executeConcatWithFallback(concatFile, outputFile, "concatenating crossfade with sequence", 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to concatenate crossfade with sequence");
        concatFile.deleteFile(); // Cleanup
        return false;
    }
    
    // Cleanup
    concatFile.deleteFile();
    
    if (logCallback) logCallback("Successfully concatenated crossfade with sequence: " + outputFile.getFileName());
    return true;
}

bool TimelineAssembler::buildProvisionalSequence(const juce::File& introSeq, const juce::File& loopFromIntro,
                                                const juce::File& loopFromLoop, int repetitions, const juce::File& outputFile)
{
    if (logCallback) logCallback("Building provisional sequence with " + juce::String(repetitions) + " loop repetitions");
    
    // More complex case: concatenate intro + loops
    juce::Array<juce::File> sequencesToConcatenate;
    
    if (introSeq.existsAsFile()) {
        sequencesToConcatenate.add(introSeq);
    }
    
    if (loopFromIntro.existsAsFile()) {
        sequencesToConcatenate.add(loopFromIntro);
    }
    
    // Add loop repetitions
    for (int i = 0; i < repetitions && loopFromLoop.existsAsFile(); i++) {
        sequencesToConcatenate.add(loopFromLoop);
    }
    
    if (sequencesToConcatenate.size() == 0) {
        if (logCallback) logCallback("ERROR: No sequences found for provisional sequence");
        return false;
    }
    
    if (sequencesToConcatenate.size() == 1) {
        // Single sequence, just copy
        if (!sequencesToConcatenate[0].copyFileTo(outputFile)) {
            if (logCallback) logCallback("ERROR: Failed to copy single sequence");
            return false;
        }
        if (logCallback) logCallback("Provisional sequence created from single sequence");
        return true;
    }
    
    // Multiple sequences, concatenate them
    juce::File concatFile = outputFile.getSiblingFile("provisional_concat.txt");
    juce::FileOutputStream concatStream(concatFile);
    if (!concatStream.openedOk()) {
        if (logCallback) logCallback("ERROR: Failed to create provisional concat file");
        return false;
    }
    
    for (const auto& seq : sequencesToConcatenate) {
        concatStream.writeText("file '" + seq.getFullPathName().replace("\\", "/") + "'\n", false, false, nullptr);
    }
    concatStream.flush();
    
    // Execute concat command
    if (!executeConcatWithFallback(concatFile, outputFile, "concatenating provisional sequences", 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to concatenate provisional sequences");
        return false;
    }

    // Cleanup the temporary concat file to save disk space
    if (concatFile.existsAsFile())
        concatFile.deleteFile();
    
    if (logCallback) logCallback("Provisional sequence created from " + juce::String(sequencesToConcatenate.size()) + " sequences");
    return true;
}

bool TimelineAssembler::trimToExactDuration(const juce::File& inputFile, const juce::File& outputFile, double duration)
{
    if (logCallback) logCallback("Trimming to exact duration: " + juce::String(duration) + "s");
    
    if (!inputFile.existsAsFile()) {
        if (logCallback) logCallback("ERROR: Input file does not exist for duration trimming");
        return false;
    }
    
    juce::String trimCommand = ffmpegExecutor->getFFmpegPath() +
        " -y -i \"" + inputFile.getFullPathName() + "\"" +
        " -to " + juce::String(duration, 6) +
        " -c:v copy -an -fflags +genpts -copyts -avoid_negative_ts make_zero" +
        " \"" + outputFile.getFullPathName() + "\"";
    
    if (!ffmpegExecutor->executeCommand(trimCommand, 0.0, 1.0)) {
        if (logCallback) logCallback("ERROR: Failed to trim to exact duration");
        return false;
    }
    
    if (logCallback) logCallback("Successfully trimmed to " + juce::String(duration) + "s");
    return true;
}

bool TimelineAssembler::applyOverlays(const juce::File& inputFile, const std::vector<RenderTypes::OverlayClipInfo>& overlayClips, const juce::File& outputFile)
{
    if (logCallback) logCallback("Applying " + juce::String(overlayClips.size()) + " overlays");
    
    if (!overlayProcessor) {
        if (logCallback) logCallback("ERROR: OverlayProcessor is null!");
        return false;
    }
    
    // Set the log callback for the overlay processor
    overlayProcessor->setLogCallback(logCallback);
    
    // Set encoding parameters
    overlayProcessor->setEncodingParams(useNvidiaAcceleration, 
                                      tempNvidiaParams, 
                                      tempCpuParams, 
                                      finalNvidiaParams, 
                                      finalCpuParams);
    
    // Get the temporary directory from the input file's parent
    juce::File tempDirectory = inputFile.getParentDirectory();
    
    // Call the overlay processor with the total duration
    return overlayProcessor->processOverlays(inputFile, overlayClips, totalDuration, tempDirectory, outputFile);
}

// Existing methods (legacy compatibility)
bool TimelineAssembler::createIntroSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                          const juce::File& tempDirectory,
                                          juce::File& outputFile)
{
    // Legacy method - redirect to new algorithm
    return assembleIntroSequence(introClips, tempDirectory);
}

bool TimelineAssembler::createLoopSequenceA(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                          const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                          const juce::File& tempDirectory,
                                          juce::File& outputFile)
{
    // Legacy method - placeholder
    if (logCallback) logCallback("Legacy createLoopSequenceA called");
    return true;
}

bool TimelineAssembler::createLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                         const juce::File& tempDirectory,
                                         juce::File& outputFile)
{
    // Legacy method - redirect to new algorithm
    std::vector<RenderTypes::VideoClipInfo> emptyIntroClips;
    return assembleLoopSequence(emptyIntroClips, loopClips, tempDirectory);
}

int TimelineAssembler::calculateRequiredLoops(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                            const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                            double totalDuration)
{
    double introDuration = calculateIntroDuration(introClips);
    double loopCycleDuration = calculateLoopDuration(loopClips);
    
    if (loopCycleDuration <= 0.001)
        return 0; // Avoid division by zero
        
    double remainingDuration = totalDuration - introDuration;
    
    if (remainingDuration <= 0.001)
        return 0; // No loops needed
        
    // Calculate required repeats and round up
    return (int)std::ceil(remainingDuration / loopCycleDuration);
}

bool TimelineAssembler::applyFadeIn(const juce::File& inputFile,
                                  const juce::File& outputFile,
                                  double fadeInDuration)
{
    if (logCallback) {
        logCallback("Applying fade-in effect of " + juce::String(fadeInDuration) + " seconds");
    }
    
    juce::String finalEncodingParams = useNvidiaAcceleration ? finalNvidiaParams : finalCpuParams;
    
    juce::String command = 
        ffmpegExecutor->getFFmpegPath() + 
        " -y" +                               // Overwrite output
        " -i \"" + inputFile.getFullPathName() + "\"" +  // Input file
        " -vf \"fade=t=in:st=0:d=" + juce::String(fadeInDuration) + "\"" +  // Fade-in filter
        " -c:v " + (useNvidiaAcceleration ? "h264_nvenc" : "libx264") + " " + finalEncodingParams +
        " -pix_fmt yuv420p" +                 // Ensure compatibility with players
        " -movflags +faststart" +            // Optimize for web streaming
        " -c:a copy" +                        // Copy audio stream
        " \"" + outputFile.getFullPathName() + "\"";
    
    return ffmpegExecutor->executeCommand(command, 0.0, 1.0);
}

bool TimelineAssembler::applyFadeOut(const juce::File& inputFile,
                                   const juce::File& outputFile,
                                   double fadeOutDuration,
                                   double totalDuration)
{
    double startTime = totalDuration - fadeOutDuration;
    
    if (startTime < 0)
        startTime = 0;
    
    if (logCallback)
        logCallback("Applying fade-out effect of " + juce::String(fadeOutDuration) + " seconds");
    
    juce::String finalEncodingParams = useNvidiaAcceleration ? finalNvidiaParams : finalCpuParams;
    
    juce::String command = 
        ffmpegExecutor->getFFmpegPath() + 
        " -y" +                               // Overwrite output
        " -i \"" + inputFile.getFullPathName() + "\"" +  // Input file
        " -vf \"fade=t=out:st=" + juce::String(startTime) + ":d=" + juce::String(fadeOutDuration) + "\"" +  // Fade-out filter
        " -c:v " + (useNvidiaAcceleration ? "h264_nvenc" : "libx264") + " " + finalEncodingParams +
        " -pix_fmt yuv420p" +                 // Ensure compatibility with players
        " -movflags +faststart" +            // Optimize for web streaming
        " -c:a copy" +                        // Copy audio stream
        " \"" + outputFile.getFullPathName() + "\"";
    
    return ffmpegExecutor->executeCommand(command, 0.0, 1.0);
}

bool TimelineAssembler::executeConcatWithFallback(const juce::File& concatList,
                                                  const juce::File& outputFile,
                                                  const juce::String& description,
                                                  double progressStart,
                                                  double progressEnd)
{
    juce::String commandBase = ffmpegExecutor->getFFmpegPath() +
        " -y -f concat -safe 0" +
        " -i \"" + concatList.getFullPathName() + "\"" +
        " " + losslessParams;
    
    juce::String primaryCommand = commandBase +
        " -an \"" + outputFile.getFullPathName() + "\"";
    
    juce::String fallbackCommand = commandBase +
        " -pix_fmt yuv420p -vsync cfr -fflags +genpts -reset_timestamps 1 -avoid_negative_ts make_zero -movflags +faststart -threads 1" +
        " -an \"" + outputFile.getFullPathName() + "\"";
    
    if (ffmpegExecutor->executeCommand(primaryCommand, progressStart, progressEnd))
        return true;
    
    if (logCallback) logCallback("WARNING: " + description + " failed; retrying with compatibility settings");
    
    if (outputFile.existsAsFile())
        outputFile.deleteFile();
    
    if (!ffmpegExecutor->executeCommand(fallbackCommand, progressStart, progressEnd))
    {
        if (logCallback) logCallback("ERROR: " + description + " failed after retry");
        return false;
    }
    
    if (logCallback) logCallback("Retry succeeded for " + description);
    return true;
}

bool TimelineAssembler::executeTrimWithFallback(const juce::File& inputFile,
                                                const juce::File& outputFile,
                                                double startSeconds,
                                                double durationSeconds,
                                                const juce::String& description)
{
    auto buildCommand = [&](bool seekBeforeInput, bool includeExtras)
    {
        juce::String command = ffmpegExecutor->getFFmpegPath() + " -y";
        
        if (seekBeforeInput && startSeconds > 0.0)
            command += " -ss " + juce::String(startSeconds, 6);
        
        command += " -i \"" + inputFile.getFullPathName() + "\"";
        
        if (!seekBeforeInput && startSeconds > 0.0)
            command += " -ss " + juce::String(startSeconds, 6);
        
        command += " -t " + juce::String(durationSeconds, 6) +
                   " " + losslessParams;
        
        if (includeExtras)
            command += " -pix_fmt yuv420p -vsync cfr -fflags +genpts -err_detect ignore_err -threads 1 -movflags +faststart";
        
        command += " -an \"" + outputFile.getFullPathName() + "\"";
        return command;
    };
    
    if (ffmpegExecutor->executeCommand(buildCommand(false, false), 0.0, 1.0))
        return true;
    
    if (logCallback) logCallback("WARNING: " + description + " failed; retrying with compatibility settings");
    
    if (outputFile.existsAsFile())
        outputFile.deleteFile();
    
    if (!ffmpegExecutor->executeCommand(buildCommand(true, true), 0.0, 1.0))
    {
        if (logCallback) logCallback("ERROR: " + description + " failed after retry");
        return false;
    }
    
    if (logCallback) logCallback("Retry succeeded for " + description);
    return true;
}

// Helper method to calculate total intro duration
double TimelineAssembler::calculateIntroDuration(const std::vector<RenderTypes::VideoClipInfo>& introClips)
{
    double duration = 0.0;
    
    // Sum duration of all intro clips
    for (const auto& clip : introClips) {
        duration += clip.duration;
    }
    
    return duration;
}

// Helper method to calculate total loop duration
double TimelineAssembler::calculateLoopDuration(const std::vector<RenderTypes::VideoClipInfo>& loopClips)
{
    double duration = 0.0;
    
    // Sum duration of all loop clips
    for (const auto& clip : loopClips) {
        duration += clip.duration;
    }
    
    return duration;
}

bool TimelineAssembler::isNvencAvailable(const juce::String& context)
{
    if (!ffmpegExecutor) {
        if (logCallback) 
            logCallback("[" + context + "] ERROR: FFmpeg executor not available for NVENC check");
        return false;
    }
    
    bool available = ffmpegExecutor->isNVENCAvailable();
    if (logCallback) {
        logCallback("[" + context + "] NVENC availability check: " + 
                   (available ? "AVAILABLE" : "NOT AVAILABLE"));
    }
    
    return available;
}
