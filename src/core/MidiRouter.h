/*
    MidiRouter.h - MIDI note/CC routing, MIDI learn integration, and E-drum handling.

    Extracted from PluginProcessor to separate MIDI routing concerns.
*/

#pragma once

#include "MidiMap.h"
#include "Sampler.h"
#include "Config.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace glorp
{

/// Result type for operations that can fail.
template <typename T, typename E>
class Result
{
public:
    Result (T value) : value_ (std::move (value)), hasValue_ (true) {}
    Result (E error) : error_ (std::move (error)), hasValue_ (false) {}

    bool isOk() const noexcept { return hasValue_; }
    bool isError() const noexcept { return ! hasValue_; }

    T& value() { return value_; }
    const T& value() const { return value_; }
    E& error() { return error_; }
    const E& error() const { return error_; }

    T valueOr (T defaultValue) const { return hasValue_ ? value_ : defaultValue; }

private:
    T value_;
    E error_;
    bool hasValue_ = false;
};

using ResultString = Result<void, juce::String>;

/// Handles MIDI routing: note-to-instrument mapping, CC-to-parameter bindings,
/// E-drum continuous controllers (CC#4 hi-hat, CC#16 snare position),
/// cymbal chokes (polyphonic aftertouch, channel pressure), and MIDI learn.
class MidiRouter
{
public:
    MidiRouter() = default;
    ~MidiRouter() = default;

    MidiRouter (const MidiRouter&) = delete;
    MidiRouter& operator= (const MidiRouter&) = delete;

    // -- Setup -----------------------------------------------------------------

    /// Sets the MIDI map for note/CC routing.
    void setMidiMap (MidiMap* midiMap) noexcept { midiMap_ = midiMap; }

    /// Sets the sampler for E-drum control (hi-hat, snare position, chokes).
    void setSampler (Sampler* sampler) noexcept { sampler_ = sampler; }

    /// Sets a callback to enqueue control values for the message thread (MIDI learn).
    void setControlQueueCallback (std::function<void (const juce::String&, float)> callback)
    {
        controlQueueCallback_ = std::move (callback);
    }

    // -- MIDI processing -------------------------------------------------------

    /// Processes a single MIDI message on the audio thread.
    /// Returns true if the message was consumed (should not trigger a note).
    bool processMessage (const juce::MidiMessage& message, int sampleOffset);

    /// Applies a MIDI map preset to the current routing.
    void applyMapPreset (const juce::String& presetName, const std::vector<juce::String>& instrumentNames);

    /// Gets the instrument index for a MIDI note.
    int instrumentForNote (int note) const noexcept;

    /// Gets the MIDI note assigned to an instrument.
    int noteForInstrument (int instrumentIndex) const noexcept;

    /// Checks if MIDI learn is active.
    bool isLearning() const noexcept;

    /// Arms MIDI learn for a control ID.
    void beginLearn (const juce::String& controlId, bool useNote = false, int channel = 0);

    /// Cancels MIDI learn.
    void cancelLearn();

    // -- E-drum state ----------------------------------------------------------

    /// Returns the current hi-hat pedal position (0.0 = open, 1.0 = closed).
    float getHiHatPedal() const noexcept;

    /// Returns the current snare position (0.0 = edge, 1.0 = center).
    float getSnarePosition() const noexcept;

    // -- Hi-Hat Mode -----------------------------------------------------------

    void setHiHatMode (Sampler::HiHatMode mode) noexcept;
    Sampler::HiHatMode getHiHatMode() const noexcept;

private:
    // -- Note/CC routing -------------------------------------------------------

    /// Routes a note message to an instrument.
    bool routeNote (const juce::MidiMessage& message, int sampleOffset);

    /// Routes a CC message to a parameter (via MIDI learn queue).
    bool routeControlChange (const juce::MidiMessage& message);

    /// Routes a note-off message.
    bool routeNoteOff (const juce::MidiMessage& message);

    // -- E-drum handling -------------------------------------------------------

    /// Handles hi-hat CC#4.
    void handleHiHatPedal (int val, int sampleOffset);

    /// Handles snare position CC#16.
    void handleSnarePosition (int val);

    /// Handles cymbal choke via polyphonic aftertouch.
    void handlePolyphonicAftertouch (const juce::MidiMessage& message, int sampleOffset);

    /// Handles cymbal choke via channel pressure.
    void handleChannelPressure (const juce::MidiMessage& message, int sampleOffset);

    // -- Helpers ---------------------------------------------------------------

    /// Queues a control value for the message thread.
    void enqueueControlValue (const juce::String& controlId, float normalised);

    // -- Members ---------------------------------------------------------------

    MidiMap* midiMap_ = nullptr;
    Sampler* sampler_ = nullptr;
    std::function<void (const juce::String&, float)> controlQueueCallback_;
};

} // namespace glorp