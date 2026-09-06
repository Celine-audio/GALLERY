#include "ImpulseResponse.h"

#include "Lagrange.h"

#include <cmath>

//==============================================================================
ImpulseResponse::ImpulseResponse()
{
    formats.registerBasicFormats();
}

double ImpulseResponse::getLengthSeconds() const noexcept
{
    if (sourceSampleRate <= 0.0)
        return 0.0;

    return (double) source.getNumSamples() / sourceSampleRate;
}

void ImpulseResponse::clear()
{
    source.setSize (0, 0);
    sourceSampleRate = 0.0;
    side = Side::both;
    name = {};
    file = juce::File();
}

//==============================================================================
juce::Result ImpulseResponse::loadFrom (const juce::File& fileToLoad, Side which)
{
    if (! fileToLoad.existsAsFile())
        return juce::Result::fail ("That file no longer exists.");

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (fileToLoad));

    if (reader == nullptr)
        return juce::Result::fail ("Not an audio file this build can read: "
                                   + fileToLoad.getFileName());

    if (reader->lengthInSamples <= 0)
        return juce::Result::fail ("That file holds no audio.");

    if (reader->sampleRate <= 0.0)
        return juce::Result::fail ("That file does not say what rate it was recorded at.");

    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;

    if (seconds > maximumSeconds)
        return juce::Result::fail ("That is " + juce::String (seconds, 1)
                                   + " seconds long. Cabinet responses over "
                                   + juce::String (maximumSeconds, 1)
                                   + " seconds are refused: four of them convolve at once, "
                                     "and the cost scales with length.");

    // At most stereo. A four-channel true-stereo response is a real thing, but it
    // needs a 2x2 convolution matrix rather than two channels, and quietly using the
    // first two would be wrong in a way nobody could hear the cause of.
    const auto available = (int) juce::jmin (reader->numChannels, 2u);

    // One side of a stereo file is a mono response, not a silenced channel: reading it
    // into a one-channel buffer is what makes the slot behave like a mono cabinet
    // afterwards. A mono file has only one answer, whatever was asked for.
    const auto takeOneSide = available > 1 && which != Side::both;
    const auto channels = takeOneSide ? 1 : available;

    juce::AudioBuffer<float> loaded (channels, (int) reader->lengthInSamples);

    if (takeOneSide)
    {
        // Which source channel is chosen by *where* the destination pointer sits: the
        // reader fills channel n from source channel n and skips a null one, so asking
        // for the right side is a null in slot zero and the buffer in slot one.
        auto* const wanted = loaded.getWritePointer (0);
        float* const destination[] = { which == Side::right ? nullptr : wanted,
                                       which == Side::right ? wanted : nullptr };

        if (! reader->read (destination, 2, 0, (int) reader->lengthInSamples))
            return juce::Result::fail ("Could not read " + fileToLoad.getFileName() + ".");
    }
    else if (! reader->read (&loaded, 0, (int) reader->lengthInSamples, 0, true, channels > 1))
    {
        return juce::Result::fail ("Could not read " + fileToLoad.getFileName() + ".");
    }

    side = available > 1 ? which : Side::both;
    source = std::move (loaded);
    sourceSampleRate = reader->sampleRate;
    name = fileToLoad.getFileName();
    file = fileToLoad;

    return juce::Result::ok();
}

int ImpulseResponse::countChannels (const juce::File& fileToRead)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    if (const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (fileToRead));
        reader != nullptr)
        return (int) reader->numChannels;

    return 0;
}

//==============================================================================
juce::AudioBuffer<float> ImpulseResponse::atSampleRate (double rate) const
{
    if (isEmpty() || rate <= 0.0 || sourceSampleRate <= 0.0)
        return {};

    if (std::abs (rate - sourceSampleRate) < 1.0e-6)
        return source;

    // A cabinet response is a recording of a speaker in a room. Played at a different
    // rate it is the same speaker transposed -- smaller and brighter, or larger and
    // duller -- which is a thing people notice without being able to say why.
    const auto ratio = rate / sourceSampleRate;
    const auto numSamples = juce::jmax (1, (int) std::llround ((double) source.getNumSamples() * ratio));

    juce::AudioBuffer<float> resampled (source.getNumChannels(), numSamples);

    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        const auto* in = source.getReadPointer (channel);
        auto* out = resampled.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
            out[i] = Lagrange::read (in, source.getNumSamples(), (double) i / ratio);
    }

    return resampled;
}
