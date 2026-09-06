#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

/**
    One loaded impulse response: the file exactly as it was read, and the summaries a
    display asks it for.

    Deliberately does no shaping. A reverb loader's version of this class trims, fades
    and stretches the response it holds, because a reverb's controls are all edits to
    the recording. A cabinet's are not: the cuts and the alignment are processing
    applied *to* the response, they belong to the slot that owns the filters, and
    keeping them out of here is what lets this class hold the file untouched -- which
    is what every one of those operations has to be recomputed from.

    Nothing here is realtime-safe. It all runs on the message thread; the audio thread
    only ever sees the finished buffer, handed over by the convolver.
*/
class ImpulseResponse
{
public:
    ImpulseResponse();

    /** The longest file worth loading into a cabinet slot.

        Four of these run at once and a convolution costs in proportion to its length,
        so the limit is four times as expensive here as it would be in a single-slot
        plugin. Two seconds is already ten times the longest cabinet capture anybody
        ships; past it the file is a room, and a room in four slots is a way to run a
        host out of CPU rather than a feature. */
    static constexpr double maximumSeconds = 2.0;

    /** Which half of a stereo file to convolve with.

        A stereo cabinet capture is two microphone positions, not a stereo image: run
        both and the slot is two different cabinets at once, one per ear. So the choice
        is the user's, and it is part of what a slot *is* -- it goes in the session
        beside the path, or reopening one would silently pick differently. */
    enum class Side { both, left, right };

    /** Reads a file. Returns a failure carrying something worth showing a user: a
        missing codec and a file that is simply too long are different problems, and
        "could not load" answers neither of them.

        `side` is ignored for a mono file, which has only one answer. */
    juce::Result loadFrom (const juce::File&, Side = Side::both);

    /** Which side the loaded response was taken from. `both` for a mono file. */
    Side getSide() const noexcept { return side; }

    /** How many channels a file holds, without loading it -- what the UI asks before
        deciding whether there is a question to put to anybody. Zero if unreadable. */
    static int countChannels (const juce::File&);

    /** Forgets the response, leaving the slot empty and silent. */
    void clear();

    bool isEmpty() const noexcept { return source.getNumSamples() == 0; }
    int getNumSamples() const noexcept { return source.getNumSamples(); }
    int getNumChannels() const noexcept { return source.getNumChannels(); }
    double getSampleRate() const noexcept { return sourceSampleRate; }
    double getLengthSeconds() const noexcept;

    /** What the strip puts under its Load button. */
    const juce::String& getName() const noexcept { return name; }

    /** Where it was read from, kept so a session can reload it. */
    const juce::File& getFile() const noexcept { return file; }

    /** The file resampled to `rate`, or a straight copy when it is already there.

        The file itself is never resampled: a response converted onto itself loses a
        little each time a host changes rate, and the losses accumulate over a session
        rather than cancelling. The pristine file is cheap to keep and the conversion is
        a message-thread cost paid once per rebuild. */
    juce::AudioBuffer<float> atSampleRate (double rate) const;

    /** One column's worth of the waveform: the lowest and highest sample in it.

        Signed rather than an absolute peak. A cabinet's polarity is the thing the Φ
        button exists for and the thing two responses have to agree about before
        aligning them means anything -- so a display drawn from absolute peaks, which is
        the same picture either way up, would hide exactly what it is there to show. */
    struct Column
    {
        float low = 0.0f, high = 0.0f;
    };


private:
    juce::AudioFormatManager formats;

    juce::AudioBuffer<float> source;
    double sourceSampleRate = 0.0;
    Side side = Side::both;
    juce::String name;
    juce::File file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImpulseResponse)
};
