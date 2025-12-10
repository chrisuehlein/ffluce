#pragma once
#include <JuceHeader.h>
#include "RenderTypes.h"
#include "FFmpegExecutor.h"
#include "OverlayProcessor.h"

/**
 * Handles the assembly of the final timeline, including
 * creating intro and loop sequences, and calculating repeats.
 * Implementation follows the approach described in video_assembly_explained.md
 */
class TimelineAssembler
{
public:
    /**
     * Creates a new TimelineAssembler.
     * @param executor The FFmpeg executor to use for timeline assembly
     * @param overlayProcessor The overlay processor to use for applying overlays
     */
    TimelineAssembler(FFmpegExecutor* executor, OverlayProcessor* overlayProcessor);
    ~TimelineAssembler();
    
    /**
     * Sets a callback for receiving log messages.
     * @param callback Function called with log messages
     */
    void setLogCallback(std::function<void(const juce::String&)> callback);
    
    /**
     * Sets the encoding parameters.
     * @param useAcceleration Whether to use NVIDIA acceleration
     * @param tempNvidiaEncodingParams NVIDIA encoding parameters for temporary files
     * @param tempCpuEncodingParams CPU encoding parameters for temporary files
     * @param finalNvidiaEncodingParams NVIDIA encoding parameters for final output
     * @param finalCpuEncodingParams CPU encoding parameters for final output
     */
    void setEncodingParams(bool useAcceleration, 
                          const juce::String& tempNvidiaEncodingParams,
                          const juce::String& tempCpuEncodingParams,
                          const juce::String& finalNvidiaEncodingParams,
                          const juce::String& finalCpuEncodingParams);
    
    /**
     * Assembles the final timeline.
     * @param introClips Information about the intro clips
     * @param loopClips Information about the loop clips
     * @param overlayClips Information about overlay clips
     * @param audioFile The audio file to include
     * @param targetDuration The total duration of the output in seconds
     * @param tempDirectory The directory to store temporary files
     * @param outputFile The file to save the final output to
     * @param fadeInDuration Duration of fade-in effect in seconds
     * @param fadeOutDuration Duration of fade-out effect in seconds
     * @return true if the operation was successful, false otherwise
     */
    bool assembleTimeline(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                         const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                         const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                         const juce::File& audioFile,
                         double targetDuration,
                         const juce::File& tempDirectory,
                         const juce::File& outputFile,
                         double fadeInDuration = 2.0,
                         double fadeOutDuration = 2.0);
    
    /**
     * Creates the intro sequence from clips and crossfades.
     * @param introClips Information about the intro clips
     * @param tempDirectory The directory with prepared clips and crossfades
     * @param outputFile The file to save the intro sequence to
     * @return true if the operation was successful, false otherwise
     */
    bool createIntroSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                            const juce::File& tempDirectory,
                            juce::File& outputFile);
    
    /**
     * Creates the loop sequence.
     * @param loopClips Information about the loop clips
     * @param tempDirectory The directory with prepared clips and crossfades
     * @param outputFile The file to save the loop sequence to
     * @return true if the operation was successful, false otherwise
     */
    // Loop sequence creation methods
    bool createLoopSequenceA(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                           const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                           const juce::File& tempDirectory,
                           juce::File& outputFile);
                           
    bool createLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                           const juce::File& tempDirectory,
                           juce::File& outputFile);
    
    /**
     * STEP 1: Conform Input Clips to Defined Durations
     * Creates temporary files matching target durations exactly
     */
    bool conformInputClips(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                          const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                          const juce::File& tempDirectory);

    /**
     * STEP 2: Generate Crossfade Components Between Clips
     * Creates all necessary crossfade segments and transitions
     */
    bool generateCrossfadeComponents(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                   const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                                   const juce::File& tempDirectory);

    /**
     * STEP 3: Assemble the Intro Sequence
     * Builds intro_sequence_raw, trims final crossfade, creates intro_sequence
     */
    bool assembleIntroSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                              const juce::File& tempDirectory);

    /**
     * STEP 4: Assemble the Loop Sequence
     * Creates loop_sequence_raw and processes variants for different transitions
     */
    bool assembleLoopSequence(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                             const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                             const juce::File& tempDirectory);

    /**
     * STEP 5: Final Loop Sequence Assembly
     * Creates loop_from_intro_sequence and loop_from_loop_sequence
     */
    bool finalLoopSequenceAssembly(const juce::File& tempDirectory,
                                  const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                  const std::vector<RenderTypes::VideoClipInfo>& loopClips);

    /**
     * STEP 6: Calculate Final Assembly and Process Overlays
     * Determines repetition count, builds final sequence, applies overlays
     */
    bool calculateFinalAssemblyAndProcessOverlays(const std::vector<RenderTypes::OverlayClipInfo>& overlayClips,
                                                 double targetDuration,
                                                 const juce::File& tempDirectory);

    /**
     * STEP 7: Mux Final Output
     * Combines video with audio and creates final output file
     */
    bool muxFinalOutput(const juce::File& audioFile,
                       const juce::File& tempDirectory,
                       const juce::File& outputFile);

    /**
     * Calculates the required number of loop repeats to reach total duration.
     * @param introClips Information about the intro clips
     * @param loopClips Information about the loop clips
     * @param targetDuration The desired total duration in seconds
     * @return The number of loop repeats required
     */
    int calculateRequiredLoops(const std::vector<RenderTypes::VideoClipInfo>& introClips,
                              const std::vector<RenderTypes::VideoClipInfo>& loopClips,
                              double targetDuration);
    
    /**
     * Applies a fade-in effect to the beginning of the video.
     * @param inputFile The input video file
     * @param outputFile The output video file
     * @param fadeInDuration The duration of the fade-in in seconds
     * @return true if the operation was successful, false otherwise
     */
    bool applyFadeIn(const juce::File& inputFile,
                    const juce::File& outputFile,
                    double fadeInDuration);
    
    /**
     * Applies a fade-out effect to the end of the video.
     * @param inputFile The input video file
     * @param outputFile The output video file
     * @param fadeOutDuration The duration of the fade-out in seconds
     * @param totalDuration The total duration of the video in seconds
     * @return true if the operation was successful, false otherwise
     */
    bool applyFadeOut(const juce::File& inputFile,
                     const juce::File& outputFile,
                     double fadeOutDuration,
                     double totalDuration);
    
    bool executeConcatWithFallback(const juce::File& concatList,
                                   const juce::File& outputFile,
                                   const juce::String& description,
                                   double progressStart = 0.0,
                                   double progressEnd = 1.0);
    
    bool executeTrimWithFallback(const juce::File& inputFile,
                                 const juce::File& outputFile,
                                 double startSeconds,
                                 double durationSeconds,
                                 const juce::String& description);
    
private:
    // Helper method to check if a crossfade should exist based on the clip settings
    bool shouldHaveCrossfade(const std::vector<RenderTypes::VideoClipInfo>& clips, size_t index) const {
        // If index is valid and crossfade duration > 0, there should be a crossfade
        return (index < clips.size() && clips[index].crossfade > 0.001);
    }
    
    /**
     * Extracts body segment from a clip (without crossfade portion)
     * Example: intro_0.mp4 -> intro_0_body_cut_out.mp4
     */
    bool extractBodySegment(const juce::File& inputClip, 
                            const juce::String& outputName,
                            double clipDuration, 
                            double crossfadeDuration, 
                            bool cutFromEnd,
                            const juce::File& tempDirectory);
    
    /**
     * Extracts crossfade entrance segment from a clip
     * Example: intro_1.mp4 -> intro_1_x_in.mp4
     */
    bool extractCrossfadeInSegment(const juce::File& inputClip,
                                  const juce::String& outputName,
                                  double crossfadeDuration,
                                  const juce::File& tempDirectory);
    
    /**
     * Extracts crossfade exit segment from a clip
     * Example: intro_0.mp4 -> intro_0_x_out.mp4 
     */
    bool extractCrossfadeOutSegment(const juce::File& inputClip,
                                   const juce::String& outputName,
                                   double clipDuration,
                                   double crossfadeDuration,
                                   const juce::File& tempDirectory);
    
    /**
     * Generates a crossfade between two segments
     * Example: intro_0_x_out.mp4 + intro_1_x_in.mp4 -> intro_0_to_intro_1_x.mp4
     */
    bool generateCrossfadeTransition(const juce::File& fromSegment,
                                    const juce::File& toSegment,
                                    const juce::String& outputName,
                                    double crossfadeDuration,
                                    const juce::File& tempDirectory);
    
    /**
     * Creates a concatenation file for the specified segments
     */
    bool createConcatFile(const juce::File& outputFile,
                         const std::vector<juce::File>& segments);
                         
    /**
     * Performs concatenation using the specified concat file
     */                     
    bool concatenateUsingFile(const juce::File& concatFile,
                             const juce::File& outputFile);
    
    // Helper method to calculate total intro duration
    double calculateIntroDuration(const std::vector<RenderTypes::VideoClipInfo>& introClips);
    
    // Helper method to calculate total loop duration
    double calculateLoopDuration(const std::vector<RenderTypes::VideoClipInfo>& loopClips);
    
    // Helper method to scan temp directory for processed clips and crossfades 
    void scanTempDirectory(const juce::File& tempDirectory);
    
    // Helper method to add debug text overlay to a filter
    juce::String addDebugText(const juce::String& label, const juce::String& filterName);
    
    // Helper method to check if NVENC is available on this system
    bool isNvencAvailable(const juce::String& context);
    
    // Helper methods for algorithm implementation
    bool generateCrossfadeForClipPair(const juce::String& type, size_t fromIndex, size_t toIndex,
                                     const RenderTypes::VideoClipInfo& fromClip, const RenderTypes::VideoClipInfo& toClip,
                                     const juce::File& tempDirectory);
    bool createMiddleClipBodySegment(const juce::String& type, size_t clipIndex, 
                                   double prevCrossfadeDuration, double nextCrossfadeDuration, 
                                   const juce::File& tempDirectory);
    bool generateIntroToLoopCrossfade(const RenderTypes::VideoClipInfo& lastIntroClip, const RenderTypes::VideoClipInfo& firstLoopClip,
                                     const juce::File& tempDirectory);
    bool generateLoopToLoopCrossfade(const RenderTypes::VideoClipInfo& lastLoopClip, const RenderTypes::VideoClipInfo& firstLoopClip,
                                    const juce::File& tempDirectory);
    bool buildRawSequence(const juce::String& type, const std::vector<RenderTypes::VideoClipInfo>& clips,
                        const juce::File& tempDirectory, const juce::File& outputFile);
    bool trimFinalCrossfade(const juce::File& inputFile, const juce::File& outputFile,
                          const juce::File& crossfadeOutputFile, double crossfadeDuration);
    bool copyFile(const juce::File& sourceFile, const juce::File& destFile);
    bool extractLoopVariants(const juce::File& loopSequenceRaw, const juce::File& tempDirectory, const juce::String& variantType,
                             const std::vector<RenderTypes::VideoClipInfo>& introClips, const std::vector<RenderTypes::VideoClipInfo>& loopClips);
public:
    bool generateInterpolatedCrossfades(const juce::File& tempDirectory, 
                                       const std::vector<RenderTypes::VideoClipInfo>& introClips,
                                       const std::vector<RenderTypes::VideoClipInfo>& loopClips);

private:
    bool concatenateTwoFiles(const juce::File& file1, const juce::File& file2, const juce::File& outputFile);
    bool concatenateWithCrossfade(const juce::File& crossfadeFile, const juce::File& sequenceFile, const juce::File& outputFile);
    bool buildProvisionalSequence(const juce::File& introSeq, const juce::File& loopFromIntro,
                                 const juce::File& loopFromLoop, int repetitions, const juce::File& outputFile);
    bool trimToExactDuration(const juce::File& inputFile, const juce::File& outputFile, double duration);
    bool applyOverlays(const juce::File& inputFile, const std::vector<RenderTypes::OverlayClipInfo>& overlayClips, const juce::File& outputFile);

    // FFmpeg executor for running commands
    FFmpegExecutor* ffmpegExecutor;
    
    // Overlay processor for applying overlays
    OverlayProcessor* overlayProcessor;
    
    // Encoding parameters
    bool useNvidiaAcceleration;
    juce::String tempNvidiaParams;
    juce::String tempCpuParams;
    juce::String finalNvidiaParams;
    juce::String finalCpuParams;
    juce::String losslessParams;
    
    // Store total duration for access in all methods
    double totalDuration;
    
    // Store fade durations for final muxing
    double fadeInDuration;
    double fadeOutDuration;
    
    // Log callback
    std::function<void(const juce::String&)> logCallback;
    
    // Debug log variables
    void createDebugLog(const juce::File& tempDirectory, const juce::String& message);
    void writeToDebugLog(const juce::String& message);
    void flushAndCopyDebugLog();
    
    juce::File debugLogFile;
    juce::FileOutputStream* debugLog = nullptr;
    bool debugLogInitialized = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineAssembler)
};
