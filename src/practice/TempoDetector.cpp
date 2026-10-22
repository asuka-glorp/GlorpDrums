/*
    TempoDetector.cpp - Smart tempo and beat detection algorithm for GlorpDrums.
    Uses 3-band log spectral flux with local mean whitening, zero-mean fractional-lag
    harmonic comb tempogram, active-window consensus, full-track pulse-train phase lock,
    and bar-aware low-band downbeat alignment.
*/

#include "TempoDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace glorp
{

namespace
{
    constexpr double kMinBpm = 55.0;
    constexpr double kMaxBpm = 215.0;
    constexpr double kBpmStep = 0.1;
    constexpr int kNumBpmBins = static_cast<int> ((kMaxBpm - kMinBpm) / kBpmStep) + 1;

    // Evaluate zero-mean normalized autocorrelation R(tau) at a continuous fractional lag tau.
    // Also checks a narrow +/- 0.5 frame neighborhood so slightly jittered human drumming is captured.
    inline float sampleFractionalAcf (const std::vector<float>& acf, double tau)
    {
        if (tau < 1.0 || acf.empty())
            return 0.0f;

        const int maxLag = static_cast<int> (acf.size()) - 1;
        const int idx = static_cast<int> (std::floor (tau));
        if (idx >= maxLag)
            return 0.0f;

        const float frac = static_cast<float> (tau - static_cast<double> (idx));
        const float y1 = acf[static_cast<size_t> (idx)];
        const float y2 = acf[static_cast<size_t> (std::min (maxLag, idx + 1))];
        float interp = y1 * (1.0f - frac) + y2 * frac;

        // Parabolic refinement if at a local peak
        if (idx >= 1 && idx + 1 <= maxLag)
        {
            const float y0 = acf[static_cast<size_t> (idx - 1)];
            const float y3 = acf[static_cast<size_t> (std::min (maxLag, idx + 2))];
            // Catmull-Rom cubic interpolation for smooth fractional lag lookup
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float cubic = ((c3 * frac + c2) * frac + c1) * frac + c0;
            interp = std::max (interp, cubic);
        }

        return std::max (0.0f, interp);
    }

    // Compute zero-mean normalized autocorrelation R[0..maxLag] for a novelty slice.
    std::vector<float> computeZeroMeanAcf (const float* data, size_t N, int maxLag, float& outRms)
    {
        outRms = 0.0f;
        if (N < 16 || maxLag < 2)
            return {};

        double sum = 0.0;
        for (size_t i = 0; i < N; ++i)
            sum += static_cast<double> (data[i]);
        const float mean = static_cast<float> (sum / static_cast<double> (N));

        std::vector<float> zeroMean (N, 0.0f);
        double varSum = 0.0;
        for (size_t i = 0; i < N; ++i)
        {
            const float v = data[i] - mean;
            zeroMean[i] = v;
            varSum += static_cast<double> (v * v);
        }

        const float variance = static_cast<float> (varSum / static_cast<double> (N));
        outRms = std::sqrt (variance);
        if (variance < 1e-8f)
            return {};

        const int clampedMaxLag = std::min (maxLag, static_cast<int> (N) - 4);
        if (clampedMaxLag < 2)
            return {};

        std::vector<float> acf (static_cast<size_t> (clampedMaxLag + 1), 0.0f);
        acf[0] = 1.0f;

        for (int lag = 1; lag <= clampedMaxLag; ++lag)
        {
            double cSum = 0.0;
            const size_t count = N - static_cast<size_t> (lag);
            for (size_t i = 0; i < count; ++i)
                cSum += static_cast<double> (zeroMean[i] * zeroMean[i + static_cast<size_t> (lag)]);

            const float normCov = static_cast<float> (cSum / static_cast<double> (count)) / variance;
            acf[static_cast<size_t> (lag)] = normCov;
        }

        return acf;
    }

    // Compute the continuous Harmonic Comb Tempogram across [kMinBpm, kMaxBpm] in 0.1 BPM steps.
    void accumulateCombSpectrum (const std::vector<float>& acf, double framesPerSec,
                                 float weight, std::vector<float>& ioSpectrum)
    {
        if (acf.empty() || ioSpectrum.size() != static_cast<size_t> (kNumBpmBins))
            return;

        for (int bIdx = 0; bIdx < kNumBpmBins; ++bIdx)
        {
            const double bpm = kMinBpm + static_cast<double> (bIdx) * kBpmStep;
            const double tau = (framesPerSec * 60.0) / bpm;

            const float r1 = sampleFractionalAcf (acf, tau);
            const float r2 = sampleFractionalAcf (acf, tau * 2.0);
            const float r3 = sampleFractionalAcf (acf, tau * 3.0);
            const float r4 = sampleFractionalAcf (acf, tau * 4.0);
            const float rHalf = sampleFractionalAcf (acf, tau * 0.5);

            // Multi-beat metric hierarchy reinforcement (1, 2, 3, 4 beats + 8th-note subdivision)
            const float comb = 1.00f * r1
                             + 0.52f * r2
                             + 0.25f * r3
                             + 0.42f * r4
                             + 0.22f * rHalf;

            // Gentle log-octave perceptual tempo prior centered at 128 BPM (width = 0.70 octaves)
            const double logDist = std::log2 (bpm / 128.0) / 0.70;
            const float prior = static_cast<float> (std::exp (-0.5 * logDist * logDist));

            const float score = comb * (0.82f + 0.18f * prior);
            ioSpectrum[static_cast<size_t> (bIdx)] += weight * score;
        }
    }

    // Evaluate phase-coherent pulse train alignment for a candidate BPM over the full track.
    // Folds the novelty envelope modulo the candidate beat period into a high-resolution phase histogram.
    float evaluatePulseTrainScore (const std::vector<float>& novelty, double framesPerSec,
                                   double candidateBpm, double* outBestPhaseFrames = nullptr)
    {
        if (novelty.empty() || candidateBpm <= 0.0)
            return 0.0f;

        const double periodFrames = (framesPerSec * 60.0) / candidateBpm;
        if (periodFrames < 4.0)
            return 0.0f;

        constexpr int kNumPhaseBins = 64;
        float hist[kNumPhaseBins] = { 0.0f };
        float hitWeights[kNumPhaseBins] = { 0.0f };

        const double invPeriod = 1.0 / periodFrames;
        const size_t N = novelty.size();

        for (size_t i = 0; i < N; ++i)
        {
            const float v = novelty[i];
            if (v <= 0.01f)
                continue;

            const double normPhase = std::fmod (static_cast<double> (i) * invPeriod, 1.0);
            const double binPos = normPhase * static_cast<double> (kNumPhaseBins);
            const int b0 = static_cast<int> (binPos) & (kNumPhaseBins - 1);
            const int b1 = (b0 + 1) & (kNumPhaseBins - 1);
            const float frac = static_cast<float> (binPos - std::floor (binPos));

            // Non-linear sharpening (v^1.3) rewards crisp transient alignment across many bars
            const float sharpV = v * std::sqrt (v);
            hist[b0] += sharpV * (1.0f - frac);
            hist[b1] += sharpV * frac;
            hitWeights[b0] += (1.0f - frac);
            hitWeights[b1] += frac;
        }

        // Smooth 3-bin circular kernel to account for natural +/- 1 frame micro-timing
        float smoothed[kNumPhaseBins] = { 0.0f };
        double sumAll = 0.0;
        for (int b = 0; b < kNumPhaseBins; ++b)
        {
            const int prev = (b - 1 + kNumPhaseBins) & (kNumPhaseBins - 1);
            const int next = (b + 1) & (kNumPhaseBins - 1);
            smoothed[b] = 0.25f * hist[prev] + 0.50f * hist[b] + 0.25f * hist[next];
            sumAll += static_cast<double> (smoothed[b]);
        }

        const float meanVal = static_cast<float> (sumAll / static_cast<double> (kNumPhaseBins));
        if (meanVal <= 1e-6f)
            return 0.0f;

        float bestScore = 0.0f;
        int bestBin = 0;

        for (int b = 0; b < kNumPhaseBins; ++b)
        {
            const int halfBin = (b + kNumPhaseBins / 2) & (kNumPhaseBins - 1);
            const int quarter1 = (b + kNumPhaseBins / 4) & (kNumPhaseBins - 1);
            const int quarter3 = (b + (3 * kNumPhaseBins) / 4) & (kNumPhaseBins - 1);

            // Primary beat phase + 8th-note subdivision phase minus off-beat 16th floor
            const float beatPeak = smoothed[b];
            const float eighthPeak = smoothed[halfBin];
            const float sixteenthFloor = 0.5f * (smoothed[quarter1] + smoothed[quarter3]);

            const float score = (beatPeak + 0.30f * eighthPeak - 0.25f * sixteenthFloor) / meanVal;
            if (score > bestScore)
            {
                bestScore = score;
                bestBin = b;
            }
        }

        if (outBestPhaseFrames != nullptr)
        {
            // Parabolic refinement of bestBin
            const int prev = (bestBin - 1 + kNumPhaseBins) & (kNumPhaseBins - 1);
            const int next = (bestBin + 1) & (kNumPhaseBins - 1);
            const float y0 = smoothed[prev];
            const float y1 = smoothed[bestBin];
            const float y2 = smoothed[next];
            const float denom = y0 - 2.0f * y1 + y2;
            double refinedBin = static_cast<double> (bestBin);
            if (std::abs (denom) > 1e-6f)
                refinedBin += std::clamp (static_cast<double> (0.5f * (y0 - y2) / denom), -0.5, 0.5);

            if (refinedBin < 0.0)
                refinedBin += static_cast<double> (kNumPhaseBins);

            *outBestPhaseFrames = (refinedBin / static_cast<double> (kNumPhaseBins)) * periodFrames;
        }

        return bestScore;
    }
} // namespace

//==============================================================================
TempoDetector::Result TempoDetector::detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Result result;

    if (buffer.getNumSamples() < static_cast<int> (sampleRate * 1.0) || sampleRate <= 0.0)
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

    const auto noveltyData = computeMultiBandNovelty (mono, analysisSampleRate, fftSize, hopSize);
    const auto& novelty = noveltyData.fullBand;
    const auto& lowBandNovelty = noveltyData.lowBand;

    if (novelty.size() < 40)
    {
        result.bpm = 120.0;
        result.confidence = 0.0f;
        result.isValid = false;
        return result;
    }

    const double framesPerSec = analysisSampleRate / static_cast<double> (hopSize);
    float confidence = 0.0f;

    // Stage 1: Coarse BPM estimation via Multi-Window + Global Fractional-Lag Harmonic Comb Tempogram
    double detectedBpm = estimateBpmAutocorrelation (novelty, framesPerSec, confidence);

    // Stage 2: Full-Track Phase-Coherent Pulse-Train Tempo Lock (0.02 BPM precision + DAW integer/.5 snap)
    detectedBpm = refineBpmPulseTrain (novelty, framesPerSec, detectedBpm, confidence);

    result.bpm = std::clamp (std::round (detectedBpm * 10.0) / 10.0, 40.0, 240.0);
    result.confidence = std::clamp (confidence, 0.0f, 1.0f);

    // Stage 3: Bar-aware Downbeat & Transient Attack Alignment
    result.firstDownbeatSeconds = findFirstDownbeat (novelty, lowBandNovelty, framesPerSec, result.bpm);
    result.isValid = result.confidence >= 0.05f;

    // Detect start beat (1, 2, 3, or 4) based on first strong onset relative to beat grid
    result.startBeat = 1;
    if (result.isValid && result.bpm > 0.0)
    {
        const double beatPeriodSec = 60.0 / result.bpm;
        const size_t maxCheckFrames = std::min (novelty.size(), static_cast<size_t> (framesPerSec * 4.0));
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

    // Subsample onset envelope for UI rendering (~800 points max)
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
TempoDetector::NoveltyData TempoDetector::computeMultiBandNovelty (const std::vector<float>& audio,
                                                                   double sr, int frameSize, int hop)
{
    NoveltyData out;
    const size_t numFrames = (audio.size() >= static_cast<size_t> (frameSize))
                               ? (audio.size() - static_cast<size_t> (frameSize)) / static_cast<size_t> (hop) + 1
                               : 0;

    if (numFrames == 0)
        return out;

    // 9th order = 512 points
    juce::dsp::FFT fft (9);

    std::vector<float> window (static_cast<size_t> (frameSize));
    for (int i = 0; i < frameSize; ++i)
    {
        // Hann window for clean spectral sidelobe suppression
        window[static_cast<size_t> (i)] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / static_cast<float> (frameSize - 1)));
    }

    const int numBins = frameSize / 2;
    const double binHz = sr / static_cast<double> (frameSize);
    const int lowBinEnd = std::clamp (static_cast<int> (std::round (220.0 / binHz)), 2, numBins - 1);
    const int midBinEnd = std::clamp (static_cast<int> (std::round (2500.0 / binHz)), lowBinEnd + 1, numBins - 1);
    const int highBinEnd = std::clamp (static_cast<int> (std::round (9500.0 / binHz)), midBinEnd + 1, numBins);

    std::vector<float> prevLogMag (static_cast<size_t> (numBins), 0.0f);
    std::vector<float> fftData (static_cast<size_t> (frameSize * 2), 0.0f);
    std::vector<float> rawFlux (numFrames, 0.0f);
    std::vector<float> rawLowFlux (numFrames, 0.0f);

    constexpr float kLogGamma = 20.0f;

    for (size_t f = 0; f < numFrames; ++f)
    {
        const size_t offset = f * static_cast<size_t> (hop);
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        for (int i = 0; i < frameSize; ++i)
            fftData[static_cast<size_t> (i)] = audio[offset + static_cast<size_t> (i)] * window[static_cast<size_t> (i)];

        fft.performRealOnlyForwardTransform (fftData.data());

        float lowFlux = 0.0f;
        float midFlux = 0.0f;
        float highFlux = 0.0f;

        for (int k = 1; k < highBinEnd; ++k)
        {
            const float re = fftData[static_cast<size_t> (k * 2)];
            const float im = fftData[static_cast<size_t> (k * 2 + 1)];
            const float mag = std::sqrt (re * re + im * im);
            const float logMag = std::log1p (kLogGamma * mag);

            const float diff = logMag - prevLogMag[static_cast<size_t> (k)];
            prevLogMag[static_cast<size_t> (k)] = logMag;

            if (diff > 0.0f)
            {
                if (k <= lowBinEnd)
                    lowFlux += diff;
                else if (k <= midBinEnd)
                    midFlux += diff;
                else
                    highFlux += diff;
            }
        }

        // Normalize each band by its bin count so the 5 low-frequency kick/bass bins
        // are not drowned out by 150 high-frequency bins
        const float normLow = lowFlux / static_cast<float> ( std::max (1, lowBinEnd));
        const float normMid = midFlux / static_cast<float> ( std::max (1, midBinEnd - lowBinEnd));
        const float normHigh = highFlux / static_cast<float> ( std::max (1, highBinEnd - midBinEnd));

        rawFlux[f] = 1.45f * normLow + 1.10f * normMid + 0.85f * normHigh;
        rawLowFlux[f] = normLow;
    }

    auto whitenAndNormalize = [numFrames](const std::vector<float>& inFlux) -> std::vector<float>
    {
        std::vector<float> whitened (numFrames, 0.0f);
        // Half-window of ~0.16s (28 frames at 172.27 fps) for adaptive local mean subtraction
        const int halfWin = 28;

        for (size_t i = 0; i < numFrames; ++i)
        {
            const size_t start = (i > static_cast<size_t> (halfWin)) ? (i - static_cast<size_t> (halfWin)) : 0;
            const size_t end = std::min (numFrames - 1, i + static_cast<size_t> (halfWin));
            float localSum = 0.0f;
            for (size_t k = start; k <= end; ++k)
                localSum += inFlux[k];
            const float localMean = localSum / static_cast<float> (end - start + 1);
            whitened[i] = std::max (0.0f, inFlux[i] - localMean * 1.05f);
        }

        // 3-frame weighted smoothing [0.25, 0.50, 0.25]
        std::vector<float> smoothed = whitened;
        for (size_t i = 1; i + 1 < numFrames; ++i)
            smoothed[i] = 0.25f * whitened[i - 1] + 0.50f * whitened[i] + 0.25f * whitened[i + 1];

        // Robust 98th-percentile normalization so single loud crashes don't crush the envelope
        std::vector<float> sortedNonZero;
        sortedNonZero.reserve (numFrames);
        for (float v : smoothed)
            if (v > 1e-6f)
                sortedNonZero.push_back (v);

        float normScale = 1.0f;
        if (! sortedNonZero.empty())
        {
            const size_t p98Idx = std::min (sortedNonZero.size() - 1, (sortedNonZero.size() * 98) / 100);
            std::nth_element (sortedNonZero.begin(),
                              sortedNonZero.begin() + static_cast<std::ptrdiff_t> (p98Idx),
                              sortedNonZero.end());
            normScale = std::max (1e-6f, sortedNonZero[p98Idx]);
        }

        for (auto& v : smoothed)
            v = std::clamp (v / normScale, 0.0f, 1.25f);

        return smoothed;
    };

    out.fullBand = whitenAndNormalize (rawFlux);
    out.lowBand = whitenAndNormalize (rawLowFlux);
    return out;
}

//==============================================================================
std::vector<float> TempoDetector::computeSpectralFlux (const std::vector<float>& audio,
                                                       double sr, int frameSize, int hop)
{
    return computeMultiBandNovelty (audio, sr, frameSize, hop).fullBand;
}

//==============================================================================
double TempoDetector::estimateBpmAutocorrelation (const std::vector<float>& novelty,
                                                  double framesPerSec, float& outConfidence)
{
    const size_t N = novelty.size();
    if (N < 30)
    {
        outConfidence = 0.0f;
        return 120.0;
    }

    // Need up to 4x the lag of kMinBpm (55 BPM) for the 4-beat harmonic comb
    const int maxCombLag = static_cast<int> (std::ceil ((framesPerSec * 60.0 / kMinBpm) * 4.1));
    std::vector<float> globalSpectrum (static_cast<size_t> (kNumBpmBins), 0.0f);

    // 1. Multi-window accumulation (12s windows, 4s hop) for tracks >= 16s
    const double totalSec = static_cast<double> (N) / framesPerSec;
    int validWindows = 0;

    if (totalSec >= 16.0)
    {
        const size_t winFrames = static_cast<size_t> (12.0 * framesPerSec);
        const size_t hopFrames = static_cast<size_t> (4.0 * framesPerSec);

        for (size_t startF = 0; startF + winFrames <= N; startF += hopFrames)
        {
            float winRms = 0.0f;
            auto winAcf = computeZeroMeanAcf (novelty.data() + startF, winFrames, maxCombLag, winRms);
            if (! winAcf.empty() && winRms >= 0.02f)
            {
                accumulateCombSpectrum (winAcf, framesPerSec, winRms, globalSpectrum);
                ++validWindows;
            }
        }
    }

    // 2. Also accumulate full-track (or central 90s) zero-mean ACF for long-range stability
    {
        const size_t maxFullFrames = static_cast<size_t> (90.0 * framesPerSec);
        const size_t startF = (N > maxFullFrames) ? (N - maxFullFrames) / 2 : 0;
        const size_t lenF = std::min (N, maxFullFrames);

        float fullRms = 0.0f;
        auto fullAcf = computeZeroMeanAcf (novelty.data() + startF, lenF, maxCombLag, fullRms);
        if (! fullAcf.empty())
        {
            const float fullWeight = (validWindows > 0) ? (fullRms * static_cast<float> (validWindows) * 0.35f) : 1.0f;
            accumulateCombSpectrum (fullAcf, framesPerSec, fullWeight, globalSpectrum);
        }
    }

    // Find peak in the accumulated Harmonic Comb Tempogram
    int bestBin = 0;
    float maxScore = -1.0f;
    double sumScore = 0.0;

    for (int i = 0; i < kNumBpmBins; ++i)
    {
        const float s = globalSpectrum[static_cast<size_t> (i)];
        sumScore += static_cast<double> (std::max (0.0f, s));
        if (s > maxScore)
        {
            maxScore = s;
            bestBin = i;
        }
    }

    if (maxScore <= 1e-6f)
    {
        outConfidence = 0.0f;
        return 120.0;
    }

    double bestBpm = kMinBpm + static_cast<double> (bestBin) * kBpmStep;

    // Helper to query spectrum value around a BPM (taking max in +/- 0.8 BPM neighborhood)
    auto querySpectrumPeak = [&](double targetBpm, double* outPeakBpm = nullptr) -> float
    {
        if (targetBpm < kMinBpm || targetBpm > kMaxBpm)
            return 0.0f;

        const int centerIdx = static_cast<int> (std::round ((targetBpm - kMinBpm) / kBpmStep));
        const int radius = 8; // +/- 0.8 BPM
        float peakVal = 0.0f;
        int peakIdx = centerIdx;

        for (int idx = std::max (0, centerIdx - radius); idx <= std::min (kNumBpmBins - 1, centerIdx + radius); ++idx)
        {
            if (globalSpectrum[static_cast<size_t> (idx)] > peakVal)
            {
                peakVal = globalSpectrum[static_cast<size_t> (idx)];
                peakIdx = idx;
            }
        }
        if (outPeakBpm != nullptr)
            *outPeakBpm = kMinBpm + static_cast<double> (peakIdx) * kBpmStep;
        return peakVal;
    };

    // Octave disambiguation:
    // Case A: If a slow half-time tempo (55..104 BPM, e.g. 90.5 BPM for 181 BPM or 57 BPM for 114 BPM) won,
    // check if double-time (2x BPM) has strong harmonic support.
    if (bestBpm <= 104.0 && (bestBpm * 2.0) <= kMaxBpm)
    {
        double doubleBpmPeak = bestBpm * 2.0;
        const float doubleScore = querySpectrumPeak (bestBpm * 2.0, &doubleBpmPeak);

        // Also check pulse-train coherence at 1x vs 2x
        const float pt1 = evaluatePulseTrainScore (novelty, framesPerSec, bestBpm);
        const float pt2 = evaluatePulseTrainScore (novelty, framesPerSec, doubleBpmPeak);

        if (doubleScore >= maxScore * 0.72f || (doubleScore >= maxScore * 0.58f && pt2 >= pt1 * 0.88f))
            bestBpm = doubleBpmPeak;
    }
    // Case B: If a very fast double-time tempo (> 195 BPM) won, check if half-time (0.5x BPM) is stronger
    else if (bestBpm >= 195.0 && (bestBpm * 0.5) >= 85.0)
    {
        double halfBpmPeak = bestBpm * 0.5;
        const float halfScore = querySpectrumPeak (bestBpm * 0.5, &halfBpmPeak);
        if (halfScore >= maxScore * 0.90f)
            bestBpm = halfBpmPeak;
    }

    const float meanScore = static_cast<float> (sumScore / static_cast<double> (kNumBpmBins));
    outConfidence = std::clamp ((maxScore - meanScore) / std::max (1e-5f, maxScore), 0.1f, 1.0f);

    return bestBpm;
}

//==============================================================================
double TempoDetector::refineBpmPulseTrain (const std::vector<float>& novelty,
                                           double framesPerSec, double coarseBpm, float& ioConfidence)
{
    if (novelty.size() < 40 || coarseBpm <= 0.0)
        return coarseBpm;

    const double minSweep = std::max (40.0, coarseBpm - 1.6);
    const double maxSweep = std::min (240.0, coarseBpm + 1.6);
    const double step = 0.02;

    double bestBpm = coarseBpm;
    float bestScore = -1.0f;

    for (double cand = minSweep; cand <= maxSweep + 1e-6; cand += step)
    {
        // Slight Gaussian proximity weight toward coarseBpm so distant side-lobes don't hijack short clips
        const double dist = (cand - coarseBpm) / 1.4;
        const float proxWeight = static_cast<float> (1.0 - 0.04 * dist * dist);
        const float rawScore = evaluatePulseTrainScore (novelty, framesPerSec, cand);
        const float weighted = rawScore * proxWeight;

        if (weighted > bestScore)
        {
            bestScore = weighted;
            bestBpm = cand;
        }
    }

    // Check DAW grid snapping: almost all recorded backing tracks use an exact integer BPM (or .5 BPM).
    // If the nearest integer BPM is within 0.55 BPM of bestBpm and achieves >= 96.5% of the peak pulse-train score,
    // lock to the exact integer BPM.
    const double nearestIntBpm = std::round (bestBpm);
    if (std::abs (bestBpm - nearestIntBpm) <= 0.55)
    {
        const float intScore = evaluatePulseTrainScore (novelty, framesPerSec, nearestIntBpm);
        const float rawBestScore = evaluatePulseTrainScore (novelty, framesPerSec, bestBpm);
        if (intScore >= rawBestScore * 0.965f)
            bestBpm = nearestIntBpm;
    }
    else
    {
        const double nearestHalfBpm = std::round (bestBpm * 2.0) / 2.0;
        if (std::abs (bestBpm - nearestHalfBpm) <= 0.25)
        {
            const float halfScore = evaluatePulseTrainScore (novelty, framesPerSec, nearestHalfBpm);
            const float rawBestScore = evaluatePulseTrainScore (novelty, framesPerSec, bestBpm);
            if (halfScore >= rawBestScore * 0.97f)
                bestBpm = nearestHalfBpm;
        }
    }

    if (bestScore > 1.2f)
        ioConfidence = std::clamp ( std::max (ioConfidence, std::min (1.0f, (bestScore - 1.0f) / 2.5f)), 0.1f, 1.0f);

    return bestBpm;
}

//==============================================================================
double TempoDetector::findFirstDownbeat (const std::vector<float>& novelty,
                                         const std::vector<float>& lowBandNovelty,
                                         double framesPerSec, double bpm)
{
    if (novelty.empty() || bpm <= 0.0 || framesPerSec <= 0.0)
        return 0.0;

    const double beatPeriodFrames = (framesPerSec * 60.0) / bpm;
    if (beatPeriodFrames < 2.0)
        return 0.0;

    // 1. Find the first strong musical attack after any leading silence
    size_t firstActiveFrame = 0;
    const size_t maxIntroCheck = std::min (novelty.size(), static_cast<size_t> (framesPerSec * 15.0));
    float introPeak = 0.0f;
    for (size_t i = 0; i < std::min (novelty.size(), static_cast<size_t> (framesPerSec * 5.0)); ++i)
        introPeak = std::max (introPeak, novelty[i]);

    const float onsetThresh = std::max (0.18f, introPeak * 0.30f);
    for (size_t i = 0; i < maxIntroCheck; ++i)
    {
        if (novelty[i] >= onsetThresh)
        {
            firstActiveFrame = i;
            break;
        }
    }

    // 2. Find optimal beat phase phi in [0, beatPeriodFrames) using the first 45 seconds of active music
    const size_t phaseAnalysisEnd = std::min (novelty.size(), firstActiveFrame + static_cast<size_t> (framesPerSec * 45.0));
    std::vector<float> earlyNovelty (novelty.begin() + static_cast<std::ptrdiff_t> (firstActiveFrame),
                                     novelty.begin() + static_cast<std::ptrdiff_t> (phaseAnalysisEnd));

    double localPhaseFrames = 0.0;
    if (earlyNovelty.size() >= static_cast<size_t> (beatPeriodFrames * 4.0))
    {
        evaluatePulseTrainScore (earlyNovelty, framesPerSec, bpm, &localPhaseFrames);
    }
    else
    {
        evaluatePulseTrainScore (novelty, framesPerSec, bpm, &localPhaseFrames);
        firstActiveFrame = 0;
    }

    // Convert localPhaseFrames back to global frame coordinates
    double baseGridFrame = static_cast<double> (firstActiveFrame) + localPhaseFrames;
    while (baseGridFrame - beatPeriodFrames >= static_cast<double> (firstActiveFrame) - 2.0)
        baseGridFrame -= beatPeriodFrames;

    // 3. Disambiguate which of the 4 beats in a 4/4 bar is Beat 1 (the true downbeat)
    // using low-frequency (kick/bass) energy + full-band accent weight + intro attack alignment.
    double bestBarFrame = baseGridFrame;
    float bestBarScore = -1.0f;

    for (int barBeat = 0; barBeat < 4; ++barBeat)
    {
        const double candidateStart = baseGridFrame + static_cast<double> (barBeat) * beatPeriodFrames;
        const double barPeriodFrames = beatPeriodFrames * 4.0;

        float barEnergy = 0.0f;
        int barCount = 0;

        for (double pos = candidateStart; pos < static_cast<double> (phaseAnalysisEnd); pos += barPeriodFrames)
        {
            const int centerIdx = static_cast<int> (std::round (pos));
            if (centerIdx >= 0 && centerIdx < static_cast<int> (novelty.size()))
            {
                // Peak within +/- 1 frame
                float fPeak = novelty[static_cast<size_t> (centerIdx)];
                float lPeak = (centerIdx < static_cast<int> (lowBandNovelty.size()))
                                ? lowBandNovelty[static_cast<size_t> (centerIdx)] : 0.0f;

                if (centerIdx > 0)
                {
                    fPeak = std::max (fPeak, novelty[static_cast<size_t> (centerIdx - 1)]);
                    if (centerIdx - 1 < static_cast<int> (lowBandNovelty.size()))
                        lPeak = std::max (lPeak, lowBandNovelty[static_cast<size_t> (centerIdx - 1)]);
                }
                if (centerIdx + 1 < static_cast<int> (novelty.size()))
                {
                    fPeak = std::max (fPeak, novelty[static_cast<size_t> (centerIdx + 1)]);
                    if (centerIdx + 1 < static_cast<int> (lowBandNovelty.size()))
                        lPeak = std::max (lPeak, lowBandNovelty[static_cast<size_t> (centerIdx + 1)]);
                }

                barEnergy += fPeak + 0.65f * lPeak;
                ++barCount;
            }
        }

        if (barCount > 0)
            barEnergy /= static_cast<float> (barCount);

        // Bonus if this bar phase lands right on the first active musical attack
        const double distToFirstOnset = std::abs (candidateStart - static_cast<double> (firstActiveFrame));
        if (distToFirstOnset <= 2.5)
            barEnergy *= 1.18f;

        if (barEnergy > bestBarScore)
        {
            bestBarScore = barEnergy;
            bestBarFrame = candidateStart;
        }
    }

    // Fold bestBarFrame back to the earliest beat grid position at or after frame 0
    // (or at firstActiveFrame if there is leading silence > 1 beat)
    const double minTargetFrame = (firstActiveFrame > static_cast<size_t> (beatPeriodFrames * 0.85))
                                    ? std::max (0.0, static_cast<double> (firstActiveFrame) - 2.0)
                                    : 0.0;

    while (bestBarFrame - beatPeriodFrames >= minTargetFrame)
        bestBarFrame -= beatPeriodFrames;

    // 4. Local transient peak refinement (+/- 2 frames = +/- 11.6 ms) so the click aligns
    // with the exact physical transient attack if an onset is present at bestBarFrame
    const int centerFrame = static_cast<int> (std::round (bestBarFrame));
    int peakFrame = centerFrame;
    float localMax = -1.0f;
    for (int f = std::max (0, centerFrame - 2); f <= std::min (static_cast<int> (novelty.size()) - 1, centerFrame + 2); ++f)
    {
        if (novelty[static_cast<size_t> (f)] > localMax)
        {
            localMax = novelty[static_cast<size_t> (f)];
            peakFrame = f;
        }
    }

    double refinedFrame = bestBarFrame;
    if (localMax >= 0.20f)
    {
        // Blend 65% global phase grid + 35% local transient peak (with parabolic sub-frame interpolation)
        double subPeak = static_cast<double> (peakFrame);
        if (peakFrame > 0 && peakFrame + 1 < static_cast<int> (novelty.size()))
        {
            const float y0 = novelty[static_cast<size_t> (peakFrame - 1)];
            const float y1 = novelty[static_cast<size_t> (peakFrame)];
            const float y2 = novelty[static_cast<size_t> (peakFrame + 1)];
            const float denom = y0 - 2.0f * y1 + y2;
            if (std::abs (denom) > 1e-5f)
                subPeak += std::clamp (static_cast<double> (0.5f * (y0 - y2) / denom), -0.5, 0.5);
        }
        refinedFrame = 0.65 * bestBarFrame + 0.35 * subPeak;
    }

    // Compensate for the STFT Hann window center group delay (fftSize / 2 samples = 2.0 frames at hopSize=128)
    const double stftGroupDelayFrames = static_cast<double> (fftSize) / static_cast<double> (2 * hopSize);
    return std::max (0.0, (refinedFrame + stftGroupDelayFrames) / framesPerSec);
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
