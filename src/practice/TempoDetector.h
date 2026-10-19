/*
    TempoDetector.h - Smart tempo and beat detection algorithm for GlorpDrums.
    Computes spectral flux novelty curve + autocorrelation + downbeat phase alignment.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <vector>

namespace glorp
{

class TempoDetector
{
public:
    struct Result
    {
        double bpm = 120.0;
        float confidence = 0.0f;           // 0.0f to 1.0f
        double firstDownbeatSeconds = 0.0; // Offset of first strong transient
        int startBeat = 1;                 // Beat within bar where music starts (1, 2, 3, 4)
        bool isValid = false;
        std::vector<float> onsetEnvelope;  // Subsampled novelty curve for UI beat markers
    };

    /// Synchronously detect tempo from an in-memory audio buffer.
    static Result detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate);

    /// Synchronously detect tempo within a specific time slice [startSec, startSec + durationSec].
    static Result detectTempoRange (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                    double startSec, double durationSec);

    /// Asynchronous worker thread that analyzes backing tracks without blocking UI.
    class Worker : public juce::Thread
    {
    public:
        Worker();
        ~Worker() override;

        void startDetection (juce::AudioBuffer<float> buffer, double sampleRate,
                             std::function<void (const Result&)> callback);

        void startDetectionRange (juce::AudioBuffer<float> buffer, double sampleRate,
                                  double startSec, double durationSec,
                                  std::function<void (const Result&)> callback);

        void cancel();
        bool isAnalyzing() const noexcept { return isBusy.load(); }

        void run() override;

    private:
        juce::AudioBuffer<float> analysisBuffer;
        double inputSampleRate = 44100.0;
        double rangeStartSec = 0.0;
        double rangeDurationSec = 0.0;
        bool useRange = false;
        std::function<void (const Result&)> onComplete;
        std::atomic<bool> isBusy { false };
        std::atomic<bool> shouldCancel { false };
    };

    static constexpr double analysisSampleRate = 22050.0;
    static constexpr int fftSize = 512;
    static constexpr int hopSize = 256;

private:
    static std::vector<float> downsampleToMono (const juce::AudioBuffer<float>& buffer,
                                                double inputSampleRate, double targetSampleRate);

    static std::vector<float> computeSpectralFlux (const std::vector<float>& monoAudio,
                                                   double sampleRate, int frameSize, int hopSize);

    static double estimateBpmAutocorrelation (const std::vector<float>& novelty,
                                              double framesPerSec, float& outConfidence);

    static double findFirstDownbeat (const std::vector<float>& novelty, double framesPerSec, double bpm);
};

} // namespace glorp
