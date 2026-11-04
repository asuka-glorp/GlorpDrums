/*
    StreamOutputBridge.h - Lock-free secondary audio output bridge for OBS / Stream Clean Mix.
    Routes the pre-metronome clean mix (Drum Kit + Backing Track, NO click track)
    to a secondary audio output device or virtual PipeWire/PulseAudio/WASAPI sink
    with zero allocations on the real-time audio thread and automatic drift compensation.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace glorp
{

class StreamOutputBridge : private juce::AudioIODeviceCallback
{
  public:
    struct DeviceEntry
    {
        juce::String typeName;
        juce::String deviceName;
        juce::String displayLabel;
    };

    StreamOutputBridge();
    ~StreamOutputBridge() override;

    StreamOutputBridge(const StreamOutputBridge &) = delete;
    StreamOutputBridge &operator=(const StreamOutputBridge &) = delete;

    /// Called from AudioEngine::prepareToPlay on the primary audio engine.
    void prepareToPlay(double primarySampleRate, int maxBlockSize);

    /// Enumerates all available audio output devices across registered JUCE device types.
    std::vector<DeviceEntry> getAvailableOutputDevices();

    /// Opens and activates a secondary output device by name (or closes if empty).
    /// Can pass "typeName::deviceName" or just "deviceName".
    bool setOutputDevice(const juce::String &deviceIdentifier, juce::String &outError);

    /// Closes any active secondary output device.
    void closeOutputDevice();

    bool isActive() const noexcept { return streamActive.load(std::memory_order_relaxed); }
    juce::String getActiveDeviceName() const;
    juce::String getLastError() const { return lastError; }

    /// When true, the main headphone output (Out 1/2) solos the clean OBS mix
    /// (suppressing the metronome click in headphones) so the user can check
    /// relative levels between the backing track and the drum kit.
    void setMonitorCleanMixInHeadphones(bool enabled) noexcept
    {
        monitorCleanInHeadphones.store(enabled, std::memory_order_relaxed);
    }
    bool isMonitoringCleanMixInHeadphones() const noexcept
    {
        return monitorCleanInHeadphones.load(std::memory_order_relaxed);
    }

    /// Output level trim for the OBS stream output (default 1.0).
    void setStreamGain(float gain) noexcept { streamGain.store(gain, std::memory_order_relaxed); }
    float getStreamGain() const noexcept { return streamGain.load(std::memory_order_relaxed); }

    /// Peak level meters of the clean mix (0.0 .. 1.0+) for UI feedback.
    float getCleanPeakLeft() const noexcept { return peakLeft.load(std::memory_order_relaxed); }
    float getCleanPeakRight() const noexcept { return peakRight.load(std::memory_order_relaxed); }

    /// Real-time safe: called from PluginProcessor::processBlock right before metronome.render().
    /// Updates clean peak meters and pushes stereo samples into the lock-free ring buffer.
    void pushAudio(const float *const *channelPointers, int numChannels, int numSamples) noexcept;

    /// Pulls available frames directly from the ring buffer (used by unit tests and offline verification).
    int pullAudioForTest(float *leftOut, float *rightOut, int maxSamples) noexcept;

    /// On Linux (PipeWire / PulseAudio), creates a virtual null sink named "GlorpDrums_OBS_Clean"
    /// via `pactl` so OBS can capture it directly, or routes via PipeWire/ALSA.
    static bool createLinuxVirtualObsSink(juce::String &statusMessage);

  private:
    // -- juce::AudioIODeviceCallback ------------------------------------------
    void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                          int numInputChannels,
                                          float *const *outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext &context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
    void audioDeviceStopped() override;

    void ensureDeviceManagerInitialised();

    std::unique_ptr<juce::AudioDeviceManager> secondaryDeviceManager;
    bool deviceManagerInitialised = false;

    std::atomic<double> sourceSampleRate {44100.0};
    std::atomic<double> targetSampleRate {44100.0};
    std::atomic<bool> streamActive {false};
    std::atomic<bool> monitorCleanInHeadphones {false};
    std::atomic<float> streamGain {1.0f};
    std::atomic<float> peakLeft {0.0f};
    std::atomic<float> peakRight {0.0f};

    juce::String activeDeviceIdentifier;
    juce::String lastError;
    mutable juce::CriticalSection stateLock;

    // Lock-free SPSC ring buffer
    static constexpr int fifoCapacity = 65536;
    juce::AbstractFifo fifo {fifoCapacity};
    juce::AudioBuffer<float> ringBuffer;

    // Fractional resampling state for secondary device callback thread
    double resamplePos = 0.0;
    float prevSampleL = 0.0f;
    float prevSampleR = 0.0f;
};

} // namespace glorp
