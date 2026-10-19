/*
    TempoDetector.cpp - Smart tempo and beat detection algorithm for GlorpDrums.
*/

#include "TempoDetector.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

//==============================================================================
TempoDetector::Result TempoDetector::detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Result result;

    if (buffer.getNumSamples() < 44100 || sampleRate <= 0.0)
    {
        result.bpm = 120.0;
        result.confidence = 0.0f;
        result.isValid = false;
        return result;
    }

    const auto mono = downsampleToMono (buffer, sampleRate, analysisSampleRate);
    if (mono.size() < static_cast<size_t> (fftSize * 4))
    {
        result.bpm = 120.0;
        result.confidence = 0.0f;
        result.isValid = false;
        return result;
    }

    const auto novelty = computeSpectralFlux (mono, analysisSampleRate, fftSize, hopSize);
    if (novelty.size() < 30)
    {
        result.bpm = 120.0;
        result.confidence = 0.0f;
        result.isValid = false;
        return result;
    }

    const double framesPerSec = analysisSampleRate / static_cast<double> (hopSize);
    const double totalSec = static_cast<double> (mono.size()) / analysisSampleRate;
    double detectedBpm = 120.0;
    float confidence = 0.0f;

    // For tracks >= 20 seconds, perform multi-window consensus analysis to isolate the main groove
    // and prevent slow intros / breakdowns from distorting the overall song tempo.
    if (totalSec >= 20.0)
    {
        const double windowSec = 10.0;
        const double hopSec = 5.0;
        const size_t windowFrames = static_cast<size_t> (windowSec * framesPerSec);
        const size_t hopFrames = static_cast<size_t> (hopSec * framesPerSec);

        struct WindowBpm { double bpm; float conf; };
        std::vector<WindowBpm> windowEstimates;

        for (size_t startF = 0; startF + windowFrames <= novelty.size(); startF += hopFrames)
        {
            std::vector<float> winNovelty (novelty.begin() + static_cast<std::ptrdiff_t> (startF),
                                           novelty.begin() + static_cast<std::ptrdiff_t> (startF + windowFrames));
            float winConf = 0.0f;
            double winBpm = estimateBpmAutocorrelation (winNovelty, framesPerSec, winConf);
            if (winConf >= 0.20f)
                windowEstimates.push_back ({ winBpm, winConf });
        }

        if (windowEstimates.size() >= 3)
        {
            // Cluster BPMs within +/- 2.5 BPM
            struct Cluster { double sumBpm = 0.0; float totalWeight = 0.0f; int count = 0; };
            std::vector<Cluster> clusters;

            for (const auto& w : windowEstimates)
            {
                bool added = false;
                for (auto& c : clusters)
                {
                    double avgBpm = c.sumBpm / static_cast<double> (c.count);
                    if (std::abs (w.bpm - avgBpm) <= 2.5)
                    {
                        c.sumBpm += w.bpm;
                        c.totalWeight += w.conf;
                        c.count++;
                        added = true;
                        break;
                    }
                }
                if (! added)
                    clusters.push_back ({ w.bpm, w.conf, 1 });
            }

            auto bestCluster = std::max_element (clusters.begin(), clusters.end(),
                [](const Cluster& a, const Cluster& b) { return a.totalWeight < b.totalWeight; });

            if (bestCluster != clusters.end() && bestCluster->count > 0)
            {
                detectedBpm = bestCluster->sumBpm / static_cast<double> (bestCluster->count);
                confidence = std::clamp (bestCluster->totalWeight / static_cast<float> (windowEstimates.size()), 0.0f, 1.0f);
            }
            else
            {
                detectedBpm = estimateBpmAutocorrelation (novelty, framesPerSec, confidence);
            }
        }
        else
        {
            detectedBpm = estimateBpmAutocorrelation (novelty, framesPerSec, confidence);
        }
    }
    else
    {
        detectedBpm = estimateBpmAutocorrelation (novelty, framesPerSec, confidence);
    }

    result.bpm = std::clamp (std::round (detectedBpm * 10.0) / 10.0, 40.0, 240.0);
    result.confidence = std::clamp (confidence, 0.0f, 1.0f);
    result.firstDownbeatSeconds = findFirstDownbeat (novelty, framesPerSec, result.bpm);
    result.isValid = result.confidence >= 0.2f;

    // Detect start beat (1, 2, 3, or 4) based on first strong onset relative to beat grid
    result.startBeat = 1;
    if (result.isValid && result.bpm > 0.0)
    {
        const double beatPeriodSec = 60.0 / result.bpm;
        const size_t maxCheckFrames = std::min (novelty.size(), static_cast<size_t> (framesPerSec * 3.5));
        const float peakThreshold = 0.25f;
        int firstOnsetFrame = -1;

        for (size_t f = 0; f < maxCheckFrames; ++f)
        {
            if (novelty[f] >= peakThreshold)
            {
                firstOnsetFrame = static_cast<int> (f);
                break;
            }
        }

        if (firstOnsetFrame >= 0)
        {
            const double firstOnsetSec = static_cast<double> (firstOnsetFrame) / framesPerSec;
            const double deltaSec = firstOnsetSec - result.firstDownbeatSeconds;
            int beatOffset = static_cast<int> (std::round (deltaSec / beatPeriodSec)) % 4;
            if (beatOffset < 0)
                beatOffset += 4;

            result.startBeat = beatOffset + 1; // 1, 2, 3, or 4
        }
    }

    // Subsample onset envelope for UI rendering (e.g. ~1000 points max)
    const size_t maxUiPoints = 800;
    if (novelty.size() <= maxUiPoints)
    {
        result.onsetEnvelope = novelty;
    }
    else
    {
        result.onsetEnvelope.resize (maxUiPoints, 0.0f);
        const double step = static_cast<double> (novelty.size()) / static_cast<double> (maxUiPoints);
        for (size_t i = 0; i < maxUiPoints; ++i)
        {
            const size_t startIdx = static_cast<size_t> (i * step);
            const size_t endIdx = std::min (novelty.size(), static_cast<size_t> ((i + 1) * step));
            float peak = 0.0f;
            for (size_t k = startIdx; k < endIdx; ++k)
                peak = std::max (peak, novelty[k]);
            result.onsetEnvelope[i] = peak;
        }
    }

    return result;
}

//==============================================================================
TempoDetector::Result TempoDetector::detectTempoRange (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                                      double startSec, double durationSec)
{
    if (sampleRate <= 0.0 || buffer.getNumSamples() <= 0)
        return {};

    const int totalSamples = buffer.getNumSamples();
    const int startSample = std::clamp (static_cast<int> (startSec * sampleRate), 0, totalSamples);
    const int numSamples = std::clamp (static_cast<int> (durationSec * sampleRate), 1, totalSamples - startSample);

    if (numSamples < static_cast<int> (sampleRate * 2.0))
        return detectTempo (buffer, sampleRate);

    juce::AudioBuffer<float> subBuf (buffer.getNumChannels(), numSamples);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        subBuf.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

    auto res = detectTempo (subBuf, sampleRate);
    res.firstDownbeatSeconds += static_cast<double> (startSample) / sampleRate;
    return res;
}

//==============================================================================
std::vector<float> TempoDetector::downsampleToMono (const juce::AudioBuffer<float>& buffer,
                                                    double inSr, double targetSr)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return {};

    const double ratio = inSr / targetSr;
    const size_t targetSamples = static_cast<size_t> (static_cast<double> (numSamples) / ratio);
    std::vector<float> mono (targetSamples, 0.0f);

    for (size_t i = 0; i < targetSamples; ++i)
    {
        const double srcPos = static_cast<double> (i) * ratio;
        const int idx0 = static_cast<int> (srcPos);
        const int idx1 = std::min (idx0 + 1, numSamples - 1);
        const float frac = static_cast<float> (srcPos - static_cast<double> (idx0));

        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* readPtr = buffer.getReadPointer (ch);
            const float val = readPtr[idx0] * (1.0f - frac) + readPtr[idx1] * frac;
            sum += val;
        }
        mono[i] = sum / static_cast<float> (numChannels);
    }

    return mono;
}

//==============================================================================
std::vector<float> TempoDetector::computeSpectralFlux (const std::vector<float>& audio,
                                                       double /*sr*/, int frameSize, int hop)
{
    const size_t numFrames = (audio.size() >= static_cast<size_t> (frameSize))
                               ? (audio.size() - static_cast<size_t> (frameSize)) / static_cast<size_t> (hop) + 1
                               : 0;

    if (numFrames == 0)
        return {};

    // 9th order = 512 points
    juce::dsp::FFT fft (9);

    std::vector<float> window (static_cast<size_t> (frameSize));
    for (int i = 0; i < frameSize; ++i)
    {
        // Hamming window
        window[static_cast<size_t> (i)] = 0.54f - 0.46f * std::cos (2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / static_cast<float> (frameSize - 1));
    }

    const int numBins = frameSize / 2;
    std::vector<float> prevMag (static_cast<size_t> (numBins), 0.0f);
    std::vector<float> currMag (static_cast<size_t> (numBins), 0.0f);
    std::vector<float> fftData (static_cast<size_t> (frameSize * 2), 0.0f);
    std::vector<float> flux (numFrames, 0.0f);

    float maxFlux = 1e-6f;

    for (size_t f = 0; f < numFrames; ++f)
    {
        const size_t offset = f * static_cast<size_t> (hop);
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        for (int i = 0; i < frameSize; ++i)
            fftData[static_cast<size_t> (i)] = audio[offset + static_cast<size_t> (i)] * window[static_cast<size_t> (i)];

        fft.performRealOnlyForwardTransform (fftData.data());

        float frameFlux = 0.0f;
        for (int k = 1; k < numBins; ++k)
        {
            const float re = fftData[static_cast<size_t> (k * 2)];
            const float im = fftData[static_cast<size_t> (k * 2 + 1)];
            const float mag = std::sqrt (re * re + im * im);
            currMag[static_cast<size_t> (k)] = mag;

            const float diff = mag - prevMag[static_cast<size_t> (k)];
            if (diff > 0.0f)
                frameFlux += diff;
        }

        prevMag = currMag;
        flux[f] = frameFlux;
        if (frameFlux > maxFlux)
            maxFlux = frameFlux;
    }

    // Normalize flux
    for (auto& v : flux)
        v /= maxFlux;

    // Moving average smoothing over 3 frames
    std::vector<float> smoothed = flux;
    for (size_t i = 1; i + 1 < flux.size(); ++i)
        smoothed[i] = (flux[i - 1] + flux[i] + flux[i + 1]) / 3.0f;

    return smoothed;
}

//==============================================================================
double TempoDetector::estimateBpmAutocorrelation (const std::vector<float>& novelty,
                                                  double framesPerSec, float& outConfidence)
{
    const size_t N = novelty.size();
    if (N < 20)
    {
        outConfidence = 0.0f;
        return 120.0;
    }

    // Min and max lag corresponding to 45-240 BPM
    const int minLag = std::max (4, static_cast<int> (std::floor (framesPerSec * 60.0 / 240.0)));
    const int maxLag = std::min (static_cast<int> (N / 2), static_cast<int> (std::ceil (framesPerSec * 60.0 / 45.0)));

    if (minLag >= maxLag)
    {
        outConfidence = 0.0f;
        return 120.0;
    }

    std::vector<float> acf (static_cast<size_t> (maxLag + 1), 0.0f);
    float maxAcf = 0.0f;
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float sum = 0.0f;
        int count = 0;
        for (size_t i = 0; i + static_cast<size_t> (lag) < N; ++i)
        {
            sum += novelty[i] * novelty[i + static_cast<size_t> (lag)];
            ++count;
        }

        if (count > 0)
            sum /= static_cast<float> (count);

        // Broad, neutral tempo prior (centered around 125 BPM with 95 BPM width, only 15% weight)
        // This avoids penalizing fast rock tempos (~181 BPM) or slow grooves (~90 BPM)
        const double candidateBpm = (framesPerSec * 60.0) / static_cast<double> (lag);
        const double priorWeight = std::exp (-std::pow ((candidateBpm - 125.0) / 95.0, 2.0) * 0.5);

        const float weighted = sum * static_cast<float> (0.85 + 0.15 * priorWeight);
        acf[static_cast<size_t> (lag)] = weighted;

        if (weighted > maxAcf)
        {
            maxAcf = weighted;
            bestLag = lag;
        }
    }

    // Harmonic octave disambiguation:
    // If a double-period lag (half BPM) was chosen, check if the fundamental period (double BPM)
    // is also a strong peak (e.g. >= 75% strength). If so, prefer the natural fast groove.
    const int halfLag = bestLag / 2;
    if (halfLag >= minLag)
    {
        const float halfAcf = acf[static_cast<size_t> (halfLag)];
        if (halfAcf >= maxAcf * 0.75f)
            bestLag = halfLag;
    }

    // Parabolic interpolation for sub-lag accuracy
    double refinedLag = static_cast<double> (bestLag);
    if (bestLag > minLag && bestLag < maxLag)
    {
        const float y0 = acf[static_cast<size_t> (bestLag - 1)];
        const float y1 = acf[static_cast<size_t> (bestLag)];
        const float y2 = acf[static_cast<size_t> (bestLag + 1)];
        const float denom = (y0 - 2.0f * y1 + y2);
        if (std::abs (denom) > 1e-6f)
        {
            const float delta = 0.5f * (y0 - y2) / denom;
            refinedLag += std::clamp (static_cast<double> (delta), -0.5, 0.5);
        }
    }

    // Compute average floor of ACF for confidence metric
    float meanAcf = 0.0f;
    for (int lag = minLag; lag <= maxLag; ++lag)
        meanAcf += acf[static_cast<size_t> (lag)];
    meanAcf /= static_cast<float> (maxLag - minLag + 1);

    outConfidence = (maxAcf > 1e-4f) ? std::clamp ((maxAcf - meanAcf) / maxAcf, 0.0f, 1.0f) : 0.0f;

    return (framesPerSec * 60.0) / refinedLag;
}

//==============================================================================
double TempoDetector::findFirstDownbeat (const std::vector<float>& novelty, double framesPerSec, double bpm)
{
    if (novelty.empty() || bpm <= 0.0)
        return 0.0;

    const double beatPeriodFrames = framesPerSec * 60.0 / bpm;
    const int searchRange = std::min (static_cast<int> (novelty.size()), static_cast<int> (beatPeriodFrames * 2.0));

    float maxEnergy = -1.0f;
    int bestOffset = 0;

    // Cross-correlate candidate phases
    for (int offset = 0; offset < searchRange; ++offset)
    {
        float energy = 0.0f;
        int hits = 0;
        for (double pos = static_cast<double> (offset); pos < static_cast<double> (novelty.size()); pos += beatPeriodFrames)
        {
            const size_t idx = static_cast<size_t> (std::round (pos));
            if (idx < novelty.size())
            {
                energy += novelty[idx];
                ++hits;
            }
        }
        if (hits > 0)
            energy /= static_cast<float> (hits);

        if (energy > maxEnergy)
        {
            maxEnergy = energy;
            bestOffset = offset;
        }
    }

    return static_cast<double> (bestOffset) / framesPerSec;
}

//==============================================================================
// Worker implementation
//==============================================================================
TempoDetector::Worker::Worker()
    : juce::Thread ("TempoDetectorWorker")
{
}

TempoDetector::Worker::~Worker()
{
    cancel();
}

void TempoDetector::Worker::startDetection (juce::AudioBuffer<float> buffer, double sampleRate,
                                           std::function<void (const Result&)> callback)
{
    cancel();

    analysisBuffer = std::move (buffer);
    inputSampleRate = sampleRate;
    useRange = false;
    onComplete = std::move (callback);
    isBusy.store (true);
    shouldCancel.store (false);

    startThread (juce::Thread::Priority::normal);
}

void TempoDetector::Worker::startDetectionRange (juce::AudioBuffer<float> buffer, double sampleRate,
                                                double startSec, double durationSec,
                                                std::function<void (const Result&)> callback)
{
    cancel();

    analysisBuffer = std::move (buffer);
    inputSampleRate = sampleRate;
    rangeStartSec = startSec;
    rangeDurationSec = durationSec;
    useRange = true;
    onComplete = std::move (callback);
    isBusy.store (true);
    shouldCancel.store (false);

    startThread (juce::Thread::Priority::normal);
}

void TempoDetector::Worker::cancel()
{
    shouldCancel.store (true);
    stopThread (1000);
    isBusy.store (false);
}

void TempoDetector::Worker::run()
{
    if (shouldCancel.load())
    {
        isBusy.store (false);
        return;
    }

    const auto result = useRange
        ? TempoDetector::detectTempoRange (analysisBuffer, inputSampleRate, rangeStartSec, rangeDurationSec)
        : TempoDetector::detectTempo (analysisBuffer, inputSampleRate);

    if (shouldCancel.load())
    {
        isBusy.store (false);
        return;
    }

    if (onComplete != nullptr)
    {
        juce::MessageManager::callAsync ([cb = onComplete, res = result]()
        {
            cb (res);
        });
    }

    isBusy.store (false);
}

} // namespace glorp
