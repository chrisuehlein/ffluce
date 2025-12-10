#include "YoutubeStreamer.h"
#include "../rendering/FFmpegExecutor.h"
#include "../rendering/OverlayProcessor.h"
#include "../core/ProcessManager.h"

YoutubeStreamer::YoutubeStreamer() : juce::Thread("YoutubeStreamer")
{
    debugLogger = std::make_unique<StreamingDebugLogger>();
}

YoutubeStreamer::~YoutubeStreamer()
{
    streamingActive = false;

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        waitForThreadToExit(10000);
    }

    cleanupFFmpegPipeline();
}

void YoutubeStreamer::setSequence(const std::vector<RenderTypes::VideoClipInfo>& intro,
                                 const std::vector<RenderTypes::VideoClipInfo>& loop,
                                 const std::vector<RenderTypes::OverlayClipInfo>& overlay)
{
    introClips = intro;
    loopClips = loop;
    overlayClips = overlay;
}

bool YoutubeStreamer::startStreaming(const juce::String& rtmpUrlOrKey, int platformId, int bitrateKbps)
{
    if (streamingActive)
    {
        if (onStatusUpdate) onStatusUpdate("Already streaming");
        return true;
    }

    if (rtmpUrlOrKey.isEmpty())
    {
        if (onError) onError("RTMP URL/key required");
        return false;
    }

    if (introClips.empty() && loopClips.empty())
    {
        if (onError) onError("No video clips configured");
        return false;
    }

    pendingRtmpKey = rtmpUrlOrKey;
    pendingPlatform = platformId;
    streamingBitrate = bitrateKbps;
    streamingActive = true;

    if (onStatusUpdate) onStatusUpdate("Starting streaming...");
    startThread();

    return true;
}

void YoutubeStreamer::stopStreaming()
{
    if (!streamingActive)
        return;

    streamingActive = false;

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        waitForThreadToExit(60000);
    }

    cleanupFFmpegPipeline();

    if (onStatusUpdate) onStatusUpdate("Streaming stopped");
}

void YoutubeStreamer::run()
{
    if (debugLogger)
    {
        debugLogger->logEvent("THREAD", "Streaming thread started");
        debugLogger->logMemoryUsage("Thread start memory");
    }

    rtmpKey = pendingRtmpKey;
    platform = pendingPlatform;

    switch (platform)
    {
        case 1: rtmpUrl = "rtmp://a.rtmp.youtube.com/live2/" + rtmpKey; break;
        case 2: rtmpUrl = "rtmp://live.twitch.tv/app/" + rtmpKey; break;
        case 3: rtmpUrl = rtmpKey; break;
        default: rtmpUrl = "rtmp://a.rtmp.youtube.com/live2/" + rtmpKey; break;
    }

    try
    {
        if (!setupFFmpegPipeline())
        {
            juce::MessageManager::callAsync([this]() {
                if (onError) onError("Failed to setup FFmpeg pipeline");
            });
            return;
        }

        int monitorCount = 0;

        while (!threadShouldExit() && streamingActive && ffmpegProcess && ffmpegProcess->isRunning())
        {
            wait(500);
            monitorCount++;

            double elapsedSeconds = monitorCount * 0.5;

            // Check for the critical 3h 25m mark
            if (debugLogger && elapsedSeconds > 12200 && elapsedSeconds < 12400)
            {
                debugLogger->logEvent("CRITICAL", "Approaching 3h 25m mark");
                debugLogger->logMemoryUsage("Critical time memory");
            }

            // Capture FFmpeg progress output
            char buffer[4096];
            int bytesRead = ffmpegProcess->readProcessOutput(buffer, sizeof(buffer) - 1);

            if (bytesRead > 0)
            {
                juce::String output;
                for (int i = 0; i < bytesRead; ++i)
                {
                    unsigned char byte = static_cast<unsigned char>(buffer[i]);
                    if (byte < 128 && (byte >= 32 || byte == 9 || byte == 10 || byte == 13))
                        output += static_cast<char>(byte);
                }

                if (output.isNotEmpty())
                    processProgressOutput(output);
            }

            if (monitorCount % 20 == 0)
            {
                juce::MessageManager::callAsync([this, elapsedSeconds]() {
                    if (onStatusUpdate)
                        onStatusUpdate("Streaming active for " + juce::String((int)elapsedSeconds) + " seconds");
                });
            }
        }

        if (ffmpegProcess && !ffmpegProcess->isRunning())
        {
            juce::MessageManager::callAsync([this]() {
                if (onError) onError("FFmpeg process stopped unexpectedly");
            });
        }
    }
    catch (const std::exception& e)
    {
        juce::MessageManager::callAsync([this]() {
            if (onError) onError("Streaming thread exception");
        });
    }
}

bool YoutubeStreamer::setupFFmpegPipeline()
{
    if (!ffmpegExecutor)
        ffmpegExecutor = std::make_unique<FFmpegExecutor>();

    if (!createAudioPipe())
    {
        if (onError) onError("Failed to initialise audio streaming pipe");
        return false;
    }

    juce::String command = buildStreamingCommand();

    if (command.isEmpty())
        return false;

    ffmpegProcess = std::make_unique<juce::ChildProcess>();

    if (!ffmpegProcess->start(command))
        return false;

    juce::Thread::sleep(1000);

    if (!ffmpegProcess->isRunning())
        return false;

    ProcessManager::getInstance().registerProcess(ffmpegProcess.get(), "YoutubeStreamer FFmpeg");

    juce::MessageManager::callAsync([this]() {
        if (onStatusUpdate)
            onStatusUpdate("Infinite streaming started - intro->loop sequence");
    });

    return true;
}

void YoutubeStreamer::cleanupFFmpegPipeline()
{
    if (ffmpegProcess)
    {
        ProcessManager::getInstance().unregisterProcess(ffmpegProcess.get());

        if (ffmpegProcess->isRunning())
        {
            ffmpegProcess->kill();

            int attempts = 0;
            while (ffmpegProcess->isRunning() && attempts < 10)
            {
                juce::Thread::sleep(500);
                attempts++;
            }

            #ifdef JUCE_WINDOWS
            if (ffmpegProcess->isRunning())
                system("taskkill /F /IM ffmpeg.exe 2>NUL");
            #endif
        }
        ffmpegProcess.reset();
    }

    closeAudioPipe();
    streamingActive = false;
}

juce::String YoutubeStreamer::buildStreamingCommand()
{
    if (!ffmpegExecutor)
        ffmpegExecutor = std::make_unique<FFmpegExecutor>();

    juce::String command = ffmpegExecutor->getFFmpegPath() + " -re";
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    if (!createStreamingSequences(tempDir))
    {
        if (onStatusUpdate) onStatusUpdate("Failed to create streaming sequences");
        return "";
    }

    if (onStatusUpdate) onStatusUpdate("Sequences ready - starting infinite stream...");

    juce::File introSequence = tempDir.getChildFile("intro_sequence.mp4");
    juce::File loopFromIntroSequence = tempDir.getChildFile("loop_from_intro_sequence.mp4");
    juce::File loopFromLoopSequence = tempDir.getChildFile("loop_from_loop_sequence.mp4");

    bool introPipelineReady = !introClips.empty() && introSequence.existsAsFile() &&
                              loopFromIntroSequence.existsAsFile() && loopFromLoopSequence.existsAsFile();

    juce::String videoMapping;
    int audioInputIndex = -1;

    if (introPipelineReady)
    {
        juce::File introPlaylistFile = tempDir.getChildFile("intro_transition_playlist.txt");
        juce::File introTransitionFile = tempDir.getChildFile("intro_transition_once.mp4");

        juce::String introPlaylist;
        introPlaylist += "file '" + introSequence.getFullPathName().replace("\\", "/") + "'\n";
        introPlaylist += "file '" + loopFromIntroSequence.getFullPathName().replace("\\", "/") + "'\n";
        introPlaylistFile.replaceWithText(introPlaylist);

        juce::String transitionEncoding = streamingUseNVENC
            ? "-c:v h264_nvenc -preset lossless -rc constqp -qp 0"
            : "-c:v libx264 -preset ultrafast -qp 0";

        juce::String createIntroTransitionCommand = ffmpegExecutor->getFFmpegPath() +
            " -y -f concat -safe 0 -i " + introPlaylistFile.getFullPathName().quoted() +
            " " + transitionEncoding + " -an " + introTransitionFile.getFullPathName().quoted();

        bool introCreated = false;
        juce::ChildProcess createIntroTransition;
        if (createIntroTransition.start(createIntroTransitionCommand))
        {
            const int introSeconds = (int) juce::roundToInt(introSequence.existsAsFile()
                                                            ? ffmpegExecutor->getFileDuration(introSequence)
                                                            : 0.0);
            const int loopSeconds = (int) juce::roundToInt(loopFromIntroSequence.existsAsFile()
                                                           ? ffmpegExecutor->getFileDuration(loopFromIntroSequence)
                                                           : 0.0);
            const int timeoutMs = juce::jmax(120000, (introSeconds + loopSeconds) * 1000);

            createIntroTransition.waitForProcessToFinish(timeoutMs);

            if (createIntroTransition.getExitCode() == 0 &&
                introTransitionFile.existsAsFile() && introTransitionFile.getSize() > 0)
            {
                introCreated = true;
            }
        }

        if (introCreated)
        {
            command += " -i " + introTransitionFile.getFullPathName().quoted();
            command += " -stream_loop -1 -i " + loopFromLoopSequence.getFullPathName().quoted();
            videoMapping = " -filter_complex \"[0:v][1:v]concat=n=2:v=1:a=0[vout]\" -map \"[vout]\"";
            audioInputIndex = 2;
        }
        else
        {
            introPipelineReady = false;
        }
    }

    if (!introPipelineReady)
    {
        if (!loopFromLoopSequence.existsAsFile())
            return "";

        command += " -stream_loop -1 -i " + loopFromLoopSequence.getFullPathName().quoted();
        videoMapping = " -map 0:v";
        audioInputIndex = 1;
    }

    if (audioPipePath.isEmpty())
        return "";

    const int pipeSampleRate = static_cast<int>(currentSampleRate > 0 ? currentSampleRate : 44100.0);
    command += " -thread_queue_size 8192 -f f32le -ar " + juce::String(pipeSampleRate) + " -ac 2 -i " + audioPipePath.quoted();
    command += videoMapping;
    command += " -map " + juce::String(audioInputIndex) + ":a";
    command += " -avoid_negative_ts make_zero -fflags +genpts";

    if (streamingBitrate <= 0)
        streamingBitrate = 9000;

    command += " -c:v h264_nvenc -preset p2 -tune ll -rc cbr";
    command += " -b:v " + juce::String(streamingBitrate) + "k";
    command += " -maxrate " + juce::String(int(streamingBitrate * 1.1)) + "k";
    command += " -bufsize " + juce::String(streamingBitrate * 2) + "k";
    command += " -g 60 -keyint_min 60";
    command += " -c:a aac -b:a 192k -ar 44100 -ac 2";
    command += " -pix_fmt yuv420p -framerate 30 -r 30";
    command += " -f flv -flvflags no_duration_filesize";
    command += " -nostdin -loglevel info -stats -stats_period 0.5";
    command += " -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 2";
    command += " " + rtmpUrl.quoted();

    return command;
}

bool YoutubeStreamer::createStreamingSequences(const juce::File& tempDir)
{
    if (onStatusUpdate) onStatusUpdate("Creating streaming sequences with crossfades...");

    if (!timelineAssembler)
    {
        ffmpegExecutor = std::make_unique<FFmpegExecutor>();
        overlayProcessor = std::make_unique<OverlayProcessor>(ffmpegExecutor.get());
        timelineAssembler = std::make_unique<TimelineAssembler>(ffmpegExecutor.get(), overlayProcessor.get());

        // Test NVENC availability
        bool shouldUseNVENC = true;
        juce::ChildProcess nvencTest;
        juce::String testCommand = ffmpegExecutor->getFFmpegPath() + " -f lavfi -i testsrc=duration=1:size=320x240:rate=1 -c:v h264_nvenc -f null -";

        if (nvencTest.start(testCommand))
        {
            bool nvencResult = nvencTest.waitForProcessToFinish(5000);
            juce::String nvencOutput = UTF8String::readAllProcessOutput(&nvencTest);

            if (!nvencResult || nvencOutput.contains("Unknown encoder") || nvencOutput.contains("not available"))
                shouldUseNVENC = false;
        }
        else
        {
            shouldUseNVENC = false;
        }

        const int targetBitrate = streamingBitrate > 0 ? streamingBitrate : 9000;
        const int peakBitrate = juce::roundToInt(targetBitrate * 1.1);
        const int bufferSize = targetBitrate * 2;

        const juce::String losslessNvencParams = "-preset lossless -rc constqp -qp 0";
        const juce::String losslessCpuParams = "-preset ultrafast -qp 0";

        const juce::String streamingNvencParams =
            "-preset p2 -tune ll -rc cbr -b:v " + juce::String(targetBitrate) + "k"
            " -maxrate " + juce::String(peakBitrate) + "k"
            " -bufsize " + juce::String(bufferSize) + "k"
            " -g 60 -keyint_min 60 -pix_fmt yuv420p";

        const juce::String streamingCpuParams =
            "-preset medium -crf 18 -pix_fmt yuv420p -g 60 -keyint_min 60";

        timelineAssembler->setEncodingParams(
            shouldUseNVENC,
            losslessNvencParams,
            losslessCpuParams,
            shouldUseNVENC ? streamingNvencParams : streamingCpuParams,
            streamingCpuParams
        );

        streamingUseNVENC = shouldUseNVENC;

        timelineAssembler->setLogCallback([this](const juce::String& message) {
            if (message.contains("ERROR") || message.contains("Failed") || message.contains("failed"))
            {
                if (onStatusUpdate) onStatusUpdate("Error: " + message);
            }
            else if (message.contains("Building provisional sequence") && onStatusUpdate)
                onStatusUpdate("Processing video sequences...");
            else if (message.contains("Creating intro sequence") && onStatusUpdate)
                onStatusUpdate("Creating intro sequence...");
            else if (message.contains("Creating loop sequences") && onStatusUpdate)
                onStatusUpdate("Creating loop sequences...");
            else if (message.contains("SUCCESS") && onStatusUpdate)
                onStatusUpdate("Sequence creation completed!");
        });
    }

    // Clean up old files
    juce::File introSequence = tempDir.getChildFile("intro_sequence.mp4");
    juce::File loopFromIntroSequence = tempDir.getChildFile("loop_from_intro_sequence.mp4");
    juce::File loopFromLoopSequence = tempDir.getChildFile("loop_from_loop_sequence.mp4");

    introSequence.deleteFile();
    loopFromIntroSequence.deleteFile();
    loopFromLoopSequence.deleteFile();

    // Delete conformed clips and crossfades
    for (int i = 0; i < 20; i++)
    {
        tempDir.getChildFile("conformed_intro_" + juce::String(i) + ".mp4").deleteFile();
        tempDir.getChildFile("conformed_loop_" + juce::String(i) + ".mp4").deleteFile();
        for (int j = 0; j < 20; j++)
            tempDir.getChildFile("crossfade_" + juce::String(i) + "_to_" + juce::String(j) + ".mp4").deleteFile();
    }

    // Delete temp files
    juce::StringArray patterns = {"temp_", "segment_", "transition_", "overlay_", "infinite_loop_", "unit_"};
    for (const auto& pattern : patterns)
    {
        juce::Array<juce::File> tempFiles;
        tempDir.findChildFiles(tempFiles, juce::File::findFiles, false, pattern + "*");
        for (const auto& file : tempFiles)
            file.deleteFile();
    }

    tempDir.getChildFile("infinite_loop_unit.mp4").deleteFile();
    tempDir.getChildFile("infinite_loop_unit_simple.mp4").deleteFile();

    // Verify video files exist
    for (const auto& clip : introClips)
    {
        if (!juce::File(clip.file).existsAsFile())
        {
            if (onStatusUpdate) onStatusUpdate("Error: Intro video file not found");
            return false;
        }
    }

    for (const auto& clip : loopClips)
    {
        if (!juce::File(clip.file).existsAsFile())
        {
            if (onStatusUpdate) onStatusUpdate("Error: Loop video file not found");
            return false;
        }
    }

    for (const auto& clip : overlayClips)
    {
        if (!juce::File(clip.file).existsAsFile())
        {
            if (onStatusUpdate) onStatusUpdate("Error: Overlay video file not found");
            return false;
        }
    }

    // Execute pipeline steps
    if (!timelineAssembler->conformInputClips(introClips, loopClips, tempDir))
        return false;

    if (!timelineAssembler->generateCrossfadeComponents(introClips, loopClips, tempDir))
        return false;

    if (!timelineAssembler->assembleIntroSequence(introClips, tempDir))
        return false;

    if (!timelineAssembler->assembleLoopSequence(introClips, loopClips, tempDir))
        return false;

    if (!timelineAssembler->finalLoopSequenceAssembly(tempDir, introClips, loopClips))
        return false;

    if (!overlayClips.empty())
    {
        if (!processOverlaysForStreaming(overlayClips, tempDir))
            return false;
    }

    return true;
}

bool YoutubeStreamer::processOverlaysForStreaming(const std::vector<RenderTypes::OverlayClipInfo>& overlayClipsParam,
                                    const juce::File& tempDir)
{
    if (!overlayProcessor)
        return false;

    std::vector<std::pair<juce::String, double>> sequences = {
        {"intro_sequence.mp4", 0.0},
        {"loop_from_intro_sequence.mp4", 0.0},
        {"loop_from_loop_sequence.mp4", 0.0}
    };

    for (const auto& seq : sequences)
    {
        juce::File inputFile = tempDir.getChildFile(seq.first);
        juce::File outputFile = tempDir.getChildFile("overlay_" + seq.first);

        if (!inputFile.existsAsFile())
            continue;

        if (!applyOverlaysToSequence(inputFile, outputFile, overlayClipsParam, seq.second))
            return false;

        if (outputFile.existsAsFile())
        {
            if (!inputFile.deleteFile() || !outputFile.moveFileTo(inputFile))
                return false;
        }
    }

    return true;
}

bool YoutubeStreamer::createAudioPipe()
{
    audioPipe.reset();
    audioPipeName = "FFLUCEAudio_" + juce::String(juce::Time::currentTimeMillis());

    audioPipe = std::make_unique<juce::NamedPipe>();

    if (!audioPipe->createNewPipe(audioPipeName, false))
    {
        audioPipe.reset();
        return false;
    }

   #if JUCE_WINDOWS
    audioPipePath = "\\\\.\\pipe\\" + juce::File::createLegalFileName(audioPipeName);
   #else
    audioPipePath = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile(juce::File::createLegalFileName(audioPipeName))
                        .getFullPathName();
   #endif

    audioPipeConnected.store(false);
    return true;
}

void YoutubeStreamer::closeAudioPipe()
{
    if (audioPipe)
    {
        audioPipe->close();
        audioPipe.reset();
    }

    audioPipePath.clear();
    audioPipeName.clear();
    audioPipeConnected.store(false);
}

bool YoutubeStreamer::applyOverlaysToSequence(const juce::File& inputSequence,
                                             const juce::File& outputSequence,
                                             const std::vector<RenderTypes::OverlayClipInfo>& overlayClipsParam,
                                             double /*timeOffset*/)
{
    if (!overlayProcessor)
        return false;

    double sequenceDuration = ffmpegExecutor->getFileDuration(inputSequence);
    if (sequenceDuration <= 0)
        return false;

    std::vector<RenderTypes::OverlayClipInfo> validOverlays;

    for (const auto& overlay : overlayClipsParam)
    {
        double requestedDuration = overlay.duration > 0.0 ? overlay.duration : sequenceDuration;
        if (requestedDuration <= sequenceDuration)
            validOverlays.push_back(overlay);
    }

    bool success = true;

    if (!validOverlays.empty())
    {
        success = overlayProcessor->processOverlays(inputSequence, validOverlays, sequenceDuration,
                                                   inputSequence.getParentDirectory(), outputSequence);
    }
    else
    {
        success = inputSequence.copyFileTo(outputSequence);
    }

    return success;
}

void YoutubeStreamer::setAudioFormat(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
}

bool YoutubeStreamer::sendAudioData(const float* const* audioData, int numSamples, int numChannels)
{
    if (!streamingActive || numSamples <= 0 || audioData == nullptr || numChannels <= 0)
        return false;

    if (audioPipe == nullptr)
        return false;

    static int fadeInSampleCount = 0;
    const int fadeInDuration = static_cast<int>((currentSampleRate > 0 ? currentSampleRate : 44100.0) * 2.0);

    const int outputChannels = 2;
    const size_t samplesToWrite = static_cast<size_t>(numSamples) * outputChannels;
    std::vector<float> tempBuffer(samplesToWrite);

    for (int i = 0; i < numSamples; ++i)
    {
        float fadeGain = 1.0f;
        if (fadeInSampleCount < fadeInDuration && fadeInDuration > 0)
        {
            fadeGain = static_cast<float>(fadeInSampleCount) / static_cast<float>(fadeInDuration);
            ++fadeInSampleCount;
        }

        const float left = (numChannels >= 1) ? audioData[0][i] : 0.0f;
        const float right = (numChannels >= 2) ? audioData[1][i] : left;

        const size_t bufferIndex = static_cast<size_t>(i) * outputChannels;
        tempBuffer[bufferIndex] = left * fadeGain;
        tempBuffer[bufferIndex + 1] = right * fadeGain;
    }

    const size_t bytesToWrite = samplesToWrite * sizeof(float);
    const uint8_t* rawData = reinterpret_cast<const uint8_t*>(tempBuffer.data());
    size_t bytesWritten = 0;

    while (bytesWritten < bytesToWrite)
    {
        if (audioPipe == nullptr || !audioPipe->isOpen())
        {
            audioPipeConnected.store(false);
            return false;
        }

        const size_t chunkSize = juce::jmin<size_t>(bytesToWrite - bytesWritten, 4096);
        const int written = audioPipe->write(rawData + bytesWritten, static_cast<int>(chunkSize), 5);

        if (written <= 0)
        {
            audioPipeConnected.store(false);
            return false;
        }

        bytesWritten += static_cast<size_t>(written);
    }

    audioPipeConnected.store(true);
    totalBytesWritten += bytesToWrite;
    audioBufferWriteCount++;
    return true;
}

void YoutubeStreamer::processProgressOutput(const juce::String& output)
{
    static juce::String pending;

    pending += output;

    for (int i = 0; i < pending.length(); )
    {
        auto cr = pending.indexOf(i, "\r");
        auto nl = pending.indexOf(i, "\n");
        int end = (cr >= 0 && (nl < 0 || cr < nl)) ? cr : (nl >= 0 ? nl : -1);

        if (end < 0) break;

        auto line = pending.substring(i, end).trim();
        if (line.isNotEmpty() && line.contains("frame=") && line.contains("fps="))
        {
            juce::String cleanLine;
            for (int charIndex = 0; charIndex < line.length(); ++charIndex)
            {
                juce::juce_wchar ch = line[charIndex];
                if ((ch >= 32 && ch <= 126) || ch == 9 || ch == 10 || ch == 13 || ch > 127)
                    cleanLine += ch;
            }

            if (cleanLine.length() > 500)
                cleanLine = cleanLine.substring(0, 500) + "...";

            if (cleanLine.isNotEmpty() && cleanLine.containsNonWhitespaceChars() && onFFmpegOutput)
            {
                juce::MessageManager::callAsync([this, cleanLine]() {
                    if (onFFmpegOutput)
                        onFFmpegOutput(cleanLine);
                });
            }
        }

        i = end + 1;
    }

    int lastSep = juce::jmax(pending.lastIndexOfChar('\r'), pending.lastIndexOfChar('\n'));
    if (lastSep >= 0)
        pending = pending.substring(lastSep + 1);
}
