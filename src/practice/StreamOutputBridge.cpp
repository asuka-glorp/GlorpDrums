/*
    StreamOutputBridge.cpp - Lock-free secondary audio output bridge for OBS / Stream Clean Mix.
*/

#include "StreamOutputBridge.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

StreamOutputBridge::StreamOutputBridge() : fifo(fifoCapacity)
{
    ringBuffer.setSize(2, fifoCapacity);
    ringBuffer.clear();
}

StreamOutputBridge::~StreamOutputBridge()
{
    closeOutputDevice();
}

void StreamOutputBridge::prepareToPlay(double primarySampleRate, int /*maxBlockSize*/)
{
    if (primarySampleRate > 0.0)
        sourceSampleRate.store(primarySampleRate, std::memory_order_relaxed);
}

void StreamOutputBridge::ensureDeviceManagerInitialised()
{
    if (secondaryDeviceManager == nullptr)
        secondaryDeviceManager = std::make_unique<juce::AudioDeviceManager>();

    if (!deviceManagerInitialised)
    {
        // Create standard device types without opening any default hardware device yet
        secondaryDeviceManager->getAvailableDeviceTypes();
        deviceManagerInitialised = true;
    }
}

std::vector<StreamOutputBridge::DeviceEntry> StreamOutputBridge::getAvailableOutputDevices()
{
    const juce::ScopedLock sl(stateLock);
    ensureDeviceManagerInitialised();

    std::vector<DeviceEntry> result;
    if (secondaryDeviceManager == nullptr)
        return result;

    const auto &types = secondaryDeviceManager->getAvailableDeviceTypes();
    for (auto *type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        const auto typeName = type->getTypeName();
        const auto deviceNames = type->getDeviceNames(false); // false = output devices

        for (const auto &devName : deviceNames)
        {
            if (devName.isEmpty())
                continue;

            DeviceEntry entry;
            entry.typeName = typeName;
            entry.deviceName = devName;
            entry.displayLabel = "[" + typeName + "] " + devName;
            result.push_back(entry);
        }
    }

    return result;
}

bool StreamOutputBridge::setOutputDevice(const juce::String &deviceIdentifier, juce::String &outError)
{
    closeOutputDevice();

    if (deviceIdentifier.isEmpty() || deviceIdentifier == "None" || deviceIdentifier == "Off")
    {
        outError.clear();
        return true;
    }

    // Special virtual test mode for headless unit verification
    if (deviceIdentifier == "VirtualTestLoopback")
    {
        const juce::ScopedLock sl(stateLock);
        fifo.reset();
        ringBuffer.clear();
        activeDeviceIdentifier = deviceIdentifier;
        lastError.clear();
        streamActive.store(true, std::memory_order_release);
        outError.clear();
        return true;
    }

    const juce::ScopedLock sl(stateLock);
    ensureDeviceManagerInitialised();

    if (secondaryDeviceManager == nullptr)
    {
        outError = "AudioDeviceManager unavailable.";
        lastError = outError;
        return false;
    }

    juce::String targetType;
    juce::String targetDev = deviceIdentifier;
    if (deviceIdentifier.contains("::"))
    {
        targetType = deviceIdentifier.upToFirstOccurrenceOf("::", false, false);
        targetDev = deviceIdentifier.fromFirstOccurrenceOf("::", false, false);
    }

    const auto &types = secondaryDeviceManager->getAvailableDeviceTypes();
    juce::AudioIODeviceType *matchedType = nullptr;
    juce::String matchedDevName;

    for (auto *t : types)
    {
        if (t == nullptr)
            continue;

        if (targetType.isNotEmpty() && t->getTypeName() != targetType)
            continue;

        t->scanForDevices();
        const auto names = t->getDeviceNames(false);
        for (const auto &name : names)
        {
            if (name == targetDev || name.containsIgnoreCase(targetDev))
            {
                matchedType = t;
                matchedDevName = name;
                break;
            }
        }
        if (matchedType != nullptr)
            break;
    }

    if (matchedType == nullptr)
    {
        outError = "Output device not found: " + deviceIdentifier;
        lastError = outError;
        return false;
    }

    secondaryDeviceManager->setCurrentAudioDeviceType(matchedType->getTypeName(), true);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    secondaryDeviceManager->getAudioDeviceSetup(setup);
    setup.outputDeviceName = matchedDevName;
    setup.inputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = true;
    if (sourceSampleRate.load(std::memory_order_relaxed) > 0.0)
        setup.sampleRate = sourceSampleRate.load(std::memory_order_relaxed);
    if (setup.bufferSize <= 0)
        setup.bufferSize = 512;

    const auto err = secondaryDeviceManager->setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty() || secondaryDeviceManager->getCurrentAudioDevice() == nullptr)
    {
        outError = err.isNotEmpty() ? err : "Failed to open output device: " + matchedDevName;
        lastError = outError;
        return false;
    }

    fifo.reset();
    ringBuffer.clear();
    resamplePos = 0.0;
    prevSampleL = 0.0f;
    prevSampleR = 0.0f;

    activeDeviceIdentifier = matchedType->getTypeName() + "::" + matchedDevName;
    lastError.clear();
    streamActive.store(true, std::memory_order_release);
    secondaryDeviceManager->addAudioCallback(this);
    outError.clear();
    return true;
}

void StreamOutputBridge::closeOutputDevice()
{
    streamActive.store(false, std::memory_order_release);

    const juce::ScopedLock sl(stateLock);
    if (secondaryDeviceManager != nullptr)
    {
        secondaryDeviceManager->removeAudioCallback(this);
        secondaryDeviceManager->closeAudioDevice();
    }
    activeDeviceIdentifier.clear();
    fifo.reset();
}

juce::String StreamOutputBridge::getActiveDeviceName() const
{
    const juce::ScopedLock sl(stateLock);
    if (!streamActive.load(std::memory_order_relaxed) || activeDeviceIdentifier.isEmpty())
        return "Off";

    if (activeDeviceIdentifier.contains("::"))
        return activeDeviceIdentifier.fromFirstOccurrenceOf("::", false, false);
    return activeDeviceIdentifier;
}

//==============================================================================
// Real-time primary audio thread producer (ZERO ALLOCATION, LOCK-FREE)
//==============================================================================
void StreamOutputBridge::pushAudio(const float *const *channelPointers, int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0 || channelPointers == nullptr)
        return;

    const float gain = streamGain.load(std::memory_order_relaxed);

    // Compute clean mix peak levels for the UI meter
    float maxL = 0.0f;
    float maxR = 0.0f;
    const float *inL = (numChannels >= 1) ? channelPointers[0] : nullptr;
    const float *inR = (numChannels >= 2) ? channelPointers[1] : inL;

    if (inL != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::abs(inL[i] * gain);
            if (absL > maxL)
                maxL = absL;
        }
    }
    if (inR != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absR = std::abs(inR[i] * gain);
            if (absR > maxR)
                maxR = absR;
        }
    }

    const float oldPeakL = peakLeft.load(std::memory_order_relaxed);
    const float oldPeakR = peakRight.load(std::memory_order_relaxed);
    peakLeft.store(std::max(maxL, oldPeakL * 0.88f), std::memory_order_relaxed);
    peakRight.store(std::max(maxR, oldPeakR * 0.88f), std::memory_order_relaxed);

    if (!streamActive.load(std::memory_order_relaxed))
        return;

    const int freeSpace = fifo.getFreeSpace();
    const int toWrite = std::min(numSamples, freeSpace);
    if (toWrite <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(toWrite, start1, size1, start2, size2);

    auto copyBlock = [&](int ringStart, int offset, int count)
    {
        float *dstL = ringBuffer.getWritePointer(0, ringStart);
        float *dstR = ringBuffer.getWritePointer(1, ringStart);

        if (inL != nullptr)
        {
            for (int i = 0; i < count; ++i)
                dstL[i] = inL[offset + i] * gain;
        }
        else
        {
            juce::FloatVectorOperations::clear(dstL, count);
        }

        if (inR != nullptr)
        {
            for (int i = 0; i < count; ++i)
                dstR[i] = inR[offset + i] * gain;
        }
        else
        {
            juce::FloatVectorOperations::clear(dstR, count);
        }
    };

    if (size1 > 0)
        copyBlock(start1, 0, size1);
    if (size2 > 0)
        copyBlock(start2, size1, size2);

    fifo.finishedWrite(size1 + size2);
}

int StreamOutputBridge::pullAudioForTest(float *leftOut, float *rightOut, int maxSamples) noexcept
{
    const int ready = fifo.getNumReady();
    const int toRead = std::min(ready, maxSamples);
    if (toRead <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(toRead, start1, size1, start2, size2);

    if (size1 > 0)
    {
        if (leftOut != nullptr)
            juce::FloatVectorOperations::copy(leftOut, ringBuffer.getReadPointer(0, start1), size1);
        if (rightOut != nullptr)
            juce::FloatVectorOperations::copy(rightOut, ringBuffer.getReadPointer(1, start1), size1);
    }
    if (size2 > 0)
    {
        if (leftOut != nullptr)
            juce::FloatVectorOperations::copy(leftOut + size1, ringBuffer.getReadPointer(0, start2), size2);
        if (rightOut != nullptr)
            juce::FloatVectorOperations::copy(rightOut + size1, ringBuffer.getReadPointer(1, start2), size2);
    }

    fifo.finishedRead(size1 + size2);
    return size1 + size2;
}

//==============================================================================
// Secondary AudioIODevice consumer callback with adaptive drift & SR ratio
//==============================================================================
void StreamOutputBridge::audioDeviceAboutToStart(juce::AudioIODevice *device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        targetSampleRate.store(device->getCurrentSampleRate(), std::memory_order_relaxed);

    fifo.reset();
    resamplePos = 0.0;
    prevSampleL = 0.0f;
    prevSampleR = 0.0f;
}

void StreamOutputBridge::audioDeviceStopped()
{
}

void StreamOutputBridge::audioDeviceIOCallbackWithContext(const float *const * /*inputChannelData*/,
                                                          int /*numInputChannels*/,
                                                          float *const *outputChannelData,
                                                          int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext & /*context*/)
{
    if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0)
        return;

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

    if (!streamActive.load(std::memory_order_relaxed))
        return;

    const int available = fifo.getNumReady();
    if (available <= 2)
        return;

    const double srcRate = std::max(8000.0, sourceSampleRate.load(std::memory_order_relaxed));
    const double dstRate = std::max(8000.0, targetSampleRate.load(std::memory_order_relaxed));

    // Target FIFO fill around ~2048 samples (~42ms at 48kHz) to absorb jitter between clocks
    constexpr int targetFill = 2048;
    const double fillError = static_cast<double>(available - targetFill) / static_cast<double>(targetFill);
    const double driftAdjust = std::clamp(fillError * 0.002, -0.015, 0.015);
    const double step = (srcRate / dstRate) * (1.0 + driftAdjust);

    const int neededSourceFrames =
        std::min(available, static_cast<int>(std::ceil(resamplePos + numSamples * step)) + 2);
    if (neededSourceFrames <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(neededSourceFrames, start1, size1, start2, size2);
    const int totalRead = size1 + size2;
    if (totalRead <= 0)
    {
        fifo.finishedRead(0);
        return;
    }

    auto readFrame = [&](int idx, float &outL, float &outR) noexcept
    {
        if (idx < 0)
        {
            outL = prevSampleL;
            outR = prevSampleR;
            return;
        }
        const int clampedIdx = std::min(idx, totalRead - 1);
        if (clampedIdx < size1)
        {
            outL = ringBuffer.getSample(0, start1 + clampedIdx);
            outR = ringBuffer.getSample(1, start1 + clampedIdx);
        }
        else
        {
            const int idx2 = clampedIdx - size1;
            outL = ringBuffer.getSample(0, start2 + idx2);
            outR = ringBuffer.getSample(1, start2 + idx2);
        }
    };

    float *outL = outputChannelData[0];
    float *outR = (numOutputChannels >= 2 && outputChannelData[1] != nullptr) ? outputChannelData[1] : outL;

    double pos = resamplePos;
    for (int i = 0; i < numSamples; ++i)
    {
        const int idx0 = static_cast<int>(std::floor(pos)) - 1;
        const int idx1 = idx0 + 1;
        const float frac = static_cast<float>(pos - std::floor(pos));

        float l0 = 0.0f, r0 = 0.0f, l1 = 0.0f, r1 = 0.0f;
        readFrame(idx0, l0, r0);
        readFrame(idx1, l1, r1);

        const float sL = l0 + frac * (l1 - l0);
        const float sR = r0 + frac * (r1 - r0);

        if (outL != nullptr)
            outL[i] = sL;
        if (outR != nullptr && outR != outL)
            outR[i] = sR;

        pos += step;
    }

    const int consumed = std::min(totalRead, static_cast<int>(std::floor(pos)));
    if (consumed > 0)
    {
        readFrame(consumed - 1, prevSampleL, prevSampleR);
        resamplePos = pos - static_cast<double>(consumed);
    }
    else
    {
        resamplePos = pos;
    }

    fifo.finishedRead(consumed);
}

//==============================================================================
// Linux PipeWire / PulseAudio Virtual Sink Helper
//==============================================================================
bool StreamOutputBridge::createLinuxVirtualObsSink(juce::String &statusMessage)
{
#if JUCE_LINUX
    juce::ChildProcess checkProc;
    if (checkProc.start("pactl list short sinks"))
    {
        const auto existing = checkProc.readAllProcessOutput();
        checkProc.waitForProcessToFinish(1500);
        if (existing.contains("GlorpDrums_OBS_Clean"))
        {
            statusMessage = "Virtual sink 'GlorpDrums_OBS_Clean' is already active in PipeWire/PulseAudio!";
            return true;
        }
    }

    juce::ChildProcess createProc;
    juce::StringArray args {"pactl",
                            "load-module",
                            "module-null-sink",
                            "sink_name=GlorpDrums_OBS_Clean",
                            "sink_properties=device.description=GlorpDrums_OBS_Clean"};
    if (createProc.start(args))
    {
        const auto output = createProc.readAllProcessOutput().trim();
        const bool finished = createProc.waitForProcessToFinish(2500);
        if (finished && createProc.getExitCode() == 0)
        {
            statusMessage = "Created virtual sink 'GlorpDrums_OBS_Clean' (Module #" + output + ").";
            return true;
        }
    }

    statusMessage =
        "Could not run 'pactl load-module module-null-sink'. Ensure PipeWire-Pulse or PulseAudio is running.";
    return false;
#else
    statusMessage = "Virtual sink creation via pactl is only supported on Linux (PipeWire/PulseAudio).";
    return false;
#endif
}

} // namespace glorp
