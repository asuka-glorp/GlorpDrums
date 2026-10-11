#include "PitchShifter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }
}

//==============================================================================
/** Minimal iterative radix-2 complex FFT, allocated once and reused.
    Exists only so the splice search can score every candidate offset in one
    pass instead of stepping through them.
*/
class Fft
{
public:
    void prepare (int size)
    {
        size_ = size;
        levels_ = 0;

        for (int n = size_; n > 1; n >>= 1)
            ++levels_;

        cos_.assign (static_cast<size_t> (size_ / 2), 0.0);
        sin_.assign (static_cast<size_t> (size_ / 2), 0.0);

        for (int i = 0; i < size_ / 2; ++i)
        {
            cos_[static_cast<size_t> (i)] = std::cos (2.0 * pi * i / size_);
            sin_[static_cast<size_t> (i)] = std::sin (2.0 * pi * i / size_);
        }

        reverse_.assign (static_cast<size_t> (size_), 0);

        for (int i = 0; i < size_; ++i)
        {
            int r = 0;

            for (int b = 0; b < levels_; ++b)
                if (i & (1 << b))
                    r |= 1 << (levels_ - 1 - b);

            reverse_[static_cast<size_t> (i)] = static_cast<size_t> (r);
        }
    }

    void forward (float* re, float* im) const { run (re, im, false); }
    void inverse (float* re, float* im) const { run (re, im, true); }

    int size() const noexcept { return size_; }

private:
    void run (float* re, float* im, bool inverse) const
    {
        const int n = size_;

        for (int i = 0; i < n; ++i)
        {
            const size_t j = reverse_[static_cast<size_t> (i)];

            if (j > static_cast<size_t> (i))
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int stride = n / len;

            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const size_t ti = static_cast<size_t> (k * stride);
                    const double wr = cos_[ti];
                    const double wi = inverse ? sin_[ti] : -sin_[ti];

                    const size_t a = static_cast<size_t> (i + k);
                    const size_t b = a + static_cast<size_t> (half);

                    const double xr = static_cast<double> (re[b]) * wr - static_cast<double> (im[b]) * wi;
                    const double xi = static_cast<double> (re[b]) * wi + static_cast<double> (im[b]) * wr;

                    re[b] = static_cast<float> (re[a] - xr);
                    im[b] = static_cast<float> (im[a] - xi);
                    re[a] = static_cast<float> (re[a] + xr);
                    im[a] = static_cast<float> (im[a] + xi);
                }
            }
        }

        if (inverse)
        {
            const double scale = 1.0 / n;

            for (int i = 0; i < n; ++i)
            {
                re[i] = static_cast<float> (re[i] * scale);
                im[i] = static_cast<float> (im[i] * scale);
            }
        }
    }

    int size_ = 0;
    int levels_ = 0;
    std::vector<double> cos_, sin_;
    std::vector<size_t> reverse_;
};

//==============================================================================
PitchShifter::PitchShifter() = default;

PitchShifter::~PitchShifter()
{
    delete fft;
}

void PitchShifter::setSearchRadius (int samples) noexcept
{
    searchRadius = samples < 0 ? 0 : samples;
}

void PitchShifter::prepare (double rate, int blockSize)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    maxBlockSize = std::max (16, blockSize);

    // A 2048 sample window with a quarter-length hop: long enough for the
    // similarity search to have real material to match, short enough that its
    // ~2048 samples of latency are inaudible on a drum hit.
    frameSize = 2048;
    hopSize = frameSize / 4;
    overlapSize = frameSize - hopSize;
    latency = frameSize;

    const int effectiveSearch = std::max (16, std::min (searchRadius, overlapSize - 1));

    fft = new Fft();
    fft->prepare (nextPowerOfTwo (2 * (overlapSize + effectiveSearch + 1)));

    const auto fftSize = static_cast<size_t> (fft->size());

    scratchUre.assign (fftSize, 0.0f);
    scratchUim.assign (fftSize, 0.0f);
    scratchVre.assign (fftSize, 0.0f);
    scratchVim.assign (fftSize, 0.0f);
    scratchWre.assign (fftSize, 0.0f);
    scratchWim.assign (fftSize, 0.0f);

    prefixEnergy.assign (static_cast<size_t> (overlapSize + effectiveSearch + 2), 0.0f);

    inputRing.assign (static_cast<size_t> (nextPowerOfTwo (static_cast<int> (sampleRate * 8.0))), 0.0f);
    dryRing.assign (static_cast<size_t> (nextPowerOfTwo (latency * 4)), 0.0f);
    fifoValue.reserve (16384);

    reset();
    setSemitones (semitones);
}

void PitchShifter::reset()
{
    inputWrite = 0;
    dryWrite = 0;
    readCursor = 0.0;
    previousFrameStart = 0;
    havePrevious = false;
    outputProduced = 0;
    resamplePosition = 0.0;
    tiltState = 0.0f;

    fifoValue.clear();
    fifoRead = 0;

    std::fill (inputRing.begin(), inputRing.end(), 0.0f);
    std::fill (dryRing.begin(), dryRing.end(), 0.0f);
}

void PitchShifter::startNote()
{
    reset();
}

void PitchShifter::setSemitones (float s)
{
    semitones = std::clamp (s, -maxSemitones, maxSemitones);

    pitchRatio = std::pow (2.0f, semitones / 12.0f);

    // WSOLA leaves pitch alone and only changes duration, so it has to *stretch*
    // by the ratio: each frame advances the output by hopSize while consuming
    // hopSize/ratio of input, making the result ratio times longer.
    analysisHop = static_cast<float> (hopSize) / pitchRatio;

    // Reading the stretched signal as y[n] = x[n * step] scales frequency *by*
    // `step`, so the step is the ratio itself. The duration it restores cancels
    // the stretch exactly, leaving a net pitch shift with the original timing.
    resampleStep = static_cast<double> (pitchRatio);

    // Upward shifts lose apparent snap, because every partial moves up while the
    // ear's brightness expectations stay anchored to where the sample was
    // recorded. A gentle high shelf proportional to the shift restores it.
    // Downward shifts get none: those genuinely should sound heavier.
    const float octaves = semitones / 12.0f;
    tiltGain = octaves > 0.0f ? std::min (0.5f, octaves * 0.6f) : 0.0f;

    if (sampleRate > 0.0)
    {
        const float fc = static_cast<float> (std::min (sampleRate * 0.45, 6500.0));
        tiltCoefficient = static_cast<float> (1.0 - std::exp (-2.0 * pi * fc / sampleRate));
    }

    dryAttackSamples = static_cast<int> (std::max (0.0f, dryAttackMs) * 0.001 * sampleRate);
}

int PitchShifter::latencySamples() const noexcept
{
    return isActive() ? latency : 0;
}

//==============================================================================
float PitchShifter::readInput (int64_t position) const noexcept
{
    if (position < 0 || position >= inputWrite)
        return 0.0f;

    const auto mask = static_cast<int64_t> (inputRing.size()) - 1;
    return inputRing[static_cast<size_t> (position & mask)];
}

int PitchShifter::searchBestOffset (int64_t nominalStart, int search)
{
    const int o = overlapSize;
    const int uLength = o + search;
    const int fftSize = fft->size();

    // A is the incoming frame at its nominal position. B is the continuation that
    // the previous frame's tail implies. For every candidate offset we want
    //
    //     corr(delta) = sum_i A[i + delta] * B[i]
    //
    // which is a cross-correlation: one FFT each way plus one inverse scores all
    // 2*search+1 candidates at once, instead of looping over them.
    std::fill (scratchUre.begin(), scratchUre.end(), 0.0f);
    std::fill (scratchUim.begin(), scratchUim.end(), 0.0f);
    std::fill (scratchVre.begin(), scratchVre.end(), 0.0f);
    std::fill (scratchVim.begin(), scratchVim.end(), 0.0f);

    for (int i = 0; i < uLength; ++i)
        scratchUre[static_cast<size_t> (i)] = readInput (nominalStart + i);

    for (int i = 0; i < o; ++i)
        scratchVre[static_cast<size_t> (i)] = readInput (previousFrameStart + hopSize + i);

    fft->forward (scratchUre.data(), scratchUim.data());
    fft->forward (scratchVre.data(), scratchVim.data());

    for (int i = 0; i < fftSize; ++i)
    {
        const double ur = scratchUre[static_cast<size_t> (i)];
        const double ui = scratchUim[static_cast<size_t> (i)];
        const double vr = scratchVre[static_cast<size_t> (i)];
        const double vi = scratchVim[static_cast<size_t> (i)];

        scratchWre[static_cast<size_t> (i)] = static_cast<float> (ur * vr + ui * vi);
        scratchWim[static_cast<size_t> (i)] = static_cast<float> (ui * vr - ur * vi);
    }

    fft->inverse (scratchWre.data(), scratchWim.data());

    // Sliding energy of A from prefix sums, so a quiet passage is not
    // automatically judged a worse splice than a loud one.
    prefixEnergy[0] = 0.0f;

    for (int i = 0; i < uLength; ++i)
    {
        const float a = scratchUre[static_cast<size_t> (i)];
        prefixEnergy[static_cast<size_t> (i) + 1] = prefixEnergy[static_cast<size_t> (i)] + a * a;
    }

    int bestOffset = 0;
    float bestScore = -std::numeric_limits<float>::infinity();

    for (int delta = -search; delta <= search; ++delta)
    {
        const int fftIndex = (delta % fftSize + fftSize) % fftSize;
        const float correlation = scratchWre[static_cast<size_t> (fftIndex)];

        const float energy = delta >= 0
            ? prefixEnergy[static_cast<size_t> (delta + o)] - prefixEnergy[static_cast<size_t> (delta)]
            : prefixEnergy[static_cast<size_t> (o)] - prefixEnergy[static_cast<size_t> (-delta)];

        const float score = energy > 1.0e-9f ? correlation / std::sqrt (energy) : 0.0f;

        if (score > bestScore)
        {
            bestScore = score;
            bestOffset = delta;
        }
    }

    return bestOffset;
}

bool PitchShifter::runFrame()
{
    const int n = frameSize;
    const int search = std::max (0, std::min (searchRadius, overlapSize - 1));

    const auto nominalStart = static_cast<int64_t> (std::llround (readCursor));

    // A whole window plus the widest possible search must have been captured.
    if (inputWrite < nominalStart + n + search + 2)
        return false;

    const int bestOffset = havePrevious ? searchBestOffset (nominalStart, search) : 0;
    const auto frameStart = nominalStart + bestOffset;

    if (! havePrevious)
    {
        // First frame: there is nothing to overlap with yet, so it lands as-is.
        // Only its leading `overlapSize` samples are final; the rest get averaged
        // with by the next frame.
        for (int i = 0; i < n; ++i)
            fifoValue.push_back (readInput (frameStart + i));
    }
    else
    {
        if (fifoValue.size() < static_cast<size_t> (overlapSize))
            return false;

        // The incoming frame's leading `overlapSize` samples land on top of the
        // previous frame's contribution over exactly the region the two share.
        // Averaging rather than summing keeps the amplitude where it belongs:
        // WSOLA's alignment means the two are near-identical there, so their mean
        // is the signal.
        const auto tail = fifoValue.size() - static_cast<size_t> (overlapSize);

        for (int i = 0; i < overlapSize; ++i)
            fifoValue[tail + static_cast<size_t> (i)] =
                (fifoValue[tail + static_cast<size_t> (i)] + readInput (frameStart + i)) * 0.5f;

        // The remaining `hopSize` samples extend the timeline.
        for (int i = overlapSize; i < n; ++i)
            fifoValue.push_back (readInput (frameStart + i));
    }

    previousFrameStart = frameStart;
    havePrevious = true;
    readCursor += analysisHop;

    return true;
}

//==============================================================================
float PitchShifter::applyTilt (float x) noexcept
{
    if (tiltGain <= 0.0f)
        return x;

    tiltState += tiltCoefficient * (x - tiltState);
    return x + tiltGain * (tiltState - x);
}

void PitchShifter::processMono (const float* in, int numInSamples, float* out, int numOutSamples)
{
    if (numOutSamples <= 0)
        return;

    // Bypass: an unshifted drum must come out bit-identical to plain sample
    // playback and must not cost the host any latency.
    if (! isActive())
    {
        const int n = std::min (numInSamples, numOutSamples);

        if (n > 0)
            std::copy (in, in + n, out);

        std::fill (out + n, out + numOutSamples, 0.0f);
        return;
    }

    {
        const auto inputMask = static_cast<int64_t> (inputRing.size()) - 1;
        const auto dryMask = static_cast<int64_t> (dryRing.size()) - 1;

        for (int i = 0; i < numInSamples; ++i)
        {
            inputRing[static_cast<size_t> (inputWrite & inputMask)] = in[i];
            ++inputWrite;

            dryRing[static_cast<size_t> (dryWrite & dryMask)] = in[i];
            dryWrite = (dryWrite + 1) % dryRing.size();
        }
    }

    // Generate enough overlap-added audio to resample the whole request from.
    const double required = static_cast<double> (numOutSamples) * resampleStep + 4.0;

    while (static_cast<double> (fifoValue.size() - fifoRead) < required)
        if (! runFrame())
            break;

    const auto fifoSize = static_cast<int64_t> (fifoValue.size());

    auto at = [&] (int64_t offset) -> float
    {
        const auto absolute = static_cast<int64_t> (fifoRead) + offset;

        if (absolute < 0 || absolute >= fifoSize)
            return 0.0f;

        return fifoValue[static_cast<size_t> (absolute)];
    };

    for (int i = 0; i < numOutSamples; ++i)
    {
        const int64_t index = outputProduced;

        if (index < latency)
        {
            // Inside the algorithm's own latency: nothing to say yet.
            out[i] = 0.0f;
        }
        else if (index - latency < dryAttackSamples)
        {
            // The attack, untouched and merely delayed.
            const auto mask = static_cast<int64_t> (dryRing.size()) - 1;
            out[i] = dryRing[static_cast<size_t> ((index - latency) & mask)];
        }
        else
        {
            const auto i0 = static_cast<int64_t> (std::floor (resamplePosition));
            const float t = static_cast<float> (resamplePosition - static_cast<double> (i0));

            const float y0 = at (i0 - 1);
            const float y1 = at (i0);
            const float y2 = at (i0 + 1);
            const float y3 = at (i0 + 2);

            // Cubic Hermite rather than linear: linear interpolation rolls off
            // above a fraction of Nyquist, which audibly dulls every upward shift
            // because the resampler reads between samples more often than not.
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            out[i] = applyTilt (((c3 * t + c2) * t + c1) * t + y1);

            resamplePosition += resampleStep;

            while (resamplePosition >= 1.0)
            {
                resamplePosition -= 1.0;
                ++fifoRead;
            }
        }

        ++outputProduced;
    }

    // Keep the FIFO bounded over a long sample. Never compact below a frame's
    // worth of tail: runFrame overlap-adds onto the tail in place and would
    // underflow the index if we did.
    if (fifoRead > 65536 && fifoValue.size() - fifoRead > static_cast<size_t> (frameSize))
    {
        fifoValue.erase (fifoValue.begin(), fifoValue.begin() + static_cast<long> (fifoRead));
        fifoRead = 0;
    }
}

} // namespace glorp
