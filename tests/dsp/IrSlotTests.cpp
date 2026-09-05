#include <dsp/IrSlot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

/*
    A slot end to end: a file goes in, and what comes out is convolved, aligned,
    panned and polarised the way the controls say.

    The alignment checks are the point of this file. Everything else on a strip can be
    judged by ear -- gain is louder, a cut is duller -- but "these two cabinets are a
    third of a millisecond apart" cannot, and a control that is a sample out is a
    control that quietly cannot do the job it exists for.
*/

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;

    /** Writes a response to a temporary wav and hands back the file. Deleted by the
        caller's TemporaryFile. */
    void writeResponse (const juce::File& file, const juce::AudioBuffer<float>& buffer,
                        double sampleRate = rate)
    {
        juce::WavAudioFormat format;

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        REQUIRE (stream != nullptr);

        const auto writer = format.createWriterFor (stream,
                                                    juce::AudioFormatWriterOptions()
                                                        .withSampleRate (sampleRate)
                                                        .withNumChannels (buffer.getNumChannels())
                                                        .withBitsPerSample (24));

        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
    }

    juce::AudioBuffer<float> makeSpike (int numSamples = 512, int position = 0)
    {
        juce::AudioBuffer<float> buffer (1, numSamples);
        buffer.clear();
        buffer.setSample (0, position, 1.0f);
        return buffer;
    }

    /** Runs an impulse through a slot and returns what comes out, which for a
        zero-latency convolution is the shaped response itself.

        Silence first, and enough of it. The engine walks to a newly handed-over filter
        over several frames rather than jumping to it -- which is what keeps a knob
        move from clicking, and which means an impulse sent immediately after one is
        convolved with a blend of the old response and the new. Measuring there would
        be measuring the crossfade. */
    juce::AudioBuffer<float> impulseThrough (IrSlot& slot, int numSamples = 4096,
                                             int numChannels = 1)
    {
        juce::AudioBuffer<float> input (numChannels, blockSize);
        juce::AudioBuffer<float> scratch (numChannels, blockSize);
        juce::AudioBuffer<float> output (numChannels, numSamples);

        output.clear();

        {
            juce::AudioBuffer<float> discard (numChannels, blockSize);

            for (int block = 0; block < 16; ++block)
            {
                input.clear();
                discard.clear();
                slot.process (input, scratch, discard);
            }
        }

        for (int start = 0; start < numSamples; start += blockSize)
        {
            input.clear();

            if (start == 0)
                for (int channel = 0; channel < numChannels; ++channel)
                    input.setSample (channel, 0, 1.0f);

            juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), numChannels,
                                            start, juce::jmin (blockSize, numSamples - start));

            juce::AudioBuffer<float> block (input.getArrayOfWritePointers(), numChannels, 0,
                                            slice.getNumSamples());

            slot.process (block, scratch, slice);
        }

        return output;
    }

    int peakPosition (const juce::AudioBuffer<float>& buffer, int channel = 0)
    {
        auto best = 0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (buffer.getSample (channel, i)) > std::abs (buffer.getSample (channel, best)))
                best = i;

        return best;
    }

    /** A slot with a response in it, levels settled, ready to be measured. */
    struct LoadedSlot
    {
        explicit LoadedSlot (const juce::AudioBuffer<float>& response, double fileRate = rate)
        {
            writeResponse (temporary.getFile().withFileExtension ("wav"), response, fileRate);
            file = temporary.getFile().withFileExtension ("wav");

            slot.prepare (rate, 1, blockSize);
            REQUIRE (slot.loadFrom (file).wasOk());
        }

        /** Pushes the ramped gains to where they are aimed, so a measurement is of the
            response rather than of the fade into it. */
        void settle (IrSlot::Levels levels)
        {
            slot.setLevels (levels);

            juce::AudioBuffer<float> input (1, blockSize), scratch (1, blockSize), output (1, blockSize);
            input.clear();

            for (int block = 0; block < 8; ++block)
            {
                output.clear();
                slot.process (input, scratch, output);
            }
        }

        juce::TemporaryFile temporary;
        juce::File file;
        IrSlot slot;
    };
}

//==============================================================================
TEST_CASE ("An empty slot is inactive and silent", "[dsp][slot]")
{
    IrSlot slot;
    slot.prepare (rate, 2, blockSize);

    CHECK_FALSE (slot.isLoaded());
    CHECK_FALSE (slot.isActive());

    const auto output = impulseThrough (slot, 1024, 2);
    CHECK (output.getMagnitude (0, output.getNumSamples()) == 0.0f);
}

TEST_CASE ("A loaded response is convolved as it was written", "[dsp][slot]")
{
    juce::AudioBuffer<float> response (1, 256);

    for (int i = 0; i < response.getNumSamples(); ++i)
        response.setSample (0, i, std::sin ((float) i * 0.05f) * 0.6f);

    LoadedSlot loaded { response };

    CHECK (loaded.slot.isLoaded());
    CHECK (loaded.slot.isActive());

    loaded.settle ({});

    const auto output = impulseThrough (loaded.slot);

    // Compared as a shape rather than sample for sample. The slot normalises what it
    // loads -- see IrSlot::normalise, which is what stops a cabinet from being twenty
    // decibels louder than no cabinet -- so what comes out is the response times some
    // constant, and it is that constant being *constant* which says the convolution is
    // right.
    // Measured at the response's own peak, not at its first sample -- which for a
    // response that starts at a zero crossing is a ratio of nothing to nothing.
    const auto reference = peakPosition (response);
    const auto scale = output.getSample (0, reference) / response.getSample (0, reference);

    REQUIRE (std::abs (scale) > 1.0e-3f);

    for (int i = 0; i < response.getNumSamples(); ++i)
    {
        INFO ("tap " << i);
        REQUIRE_THAT (output.getSample (0, i),
                      Catch::Matchers::WithinAbs (response.getSample (0, i) * scale, 2.0e-3f));
    }
}

TEST_CASE ("A loaded response is levelled to its loudest frequency", "[dsp][slot]")
{
    // What answers "it gets way too loud with an IR". A convolution sums a tap per
    // sample of the response, so a raw capture carries whatever gain its own length
    // and level add up to -- routinely twenty or thirty decibels for a cabinet.
    //
    // The rule is that the response's loudest frequency comes out at unity, so a
    // cabinet can only ever take away. Checked on the measured curve, which is the same
    // curve the graph draws and the same one the levelling was derived from.
    std::vector<float> levels;

    for (const auto amplitude : { 0.1f, 0.5f, 1.0f })
    {
        juce::AudioBuffer<float> response (1, 512);
        juce::Random random { 8 };

        for (int i = 0; i < response.getNumSamples(); ++i)
        {
            const auto envelope = std::exp (-8.0f * (float) i / (float) response.getNumSamples());
            response.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * envelope * amplitude);
        }

        LoadedSlot loaded { response };
        loaded.settle ({});

        const auto& spectrum = loaded.slot.getSpectrumDb();
        const auto loudest = *std::max_element (spectrum.begin(), spectrum.end());

        INFO ("source amplitude " << amplitude << ", peak " << loudest << " dB");
        CHECK_THAT (loudest, Catch::Matchers::WithinAbs (0.0f, 0.5f));

        const auto output = impulseThrough (loaded.slot);

        auto energy = 0.0;

        for (int i = 0; i < output.getNumSamples(); ++i)
            energy += (double) output.getSample (0, i) * (double) output.getSample (0, i);

        levels.push_back ((float) energy);
    }

    // ...and three captures of quite different levels all arrive at the same loudness,
    // which is the part somebody notices.
    for (size_t i = 1; i < levels.size(); ++i)
        CHECK_THAT (levels[i], Catch::Matchers::WithinRel (levels[0], 0.02f));
}

TEST_CASE ("A flat response passes audio at unity", "[dsp][slot]")
{
    // The sanity check on the levelling rule: a response whose magnitude is the same
    // at every frequency has nothing to take away, so normalising its loudest point to
    // unity has to leave it passing signal through untouched.
    LoadedSlot loaded { makeSpike() };
    loaded.settle ({});

    const auto output = impulseThrough (loaded.slot);

    CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (1.0f, 0.02f));
}

// The pan law is checked where it actually runs, in ProcessorTests: a mono slot skips
// panning altogether, so asking about it here would be asking a question the code
// under test does not answer.

TEST_CASE ("Alignment delays the response by exactly what it says", "[dsp][slot][align]")
{
    // Through the audio path rather than baked into the response: alignment is a delay
    // line now, so what it does is only visible in what comes out. A whole number of
    // samples, so the answer is an integer and an off-by-one has nowhere to hide.
    for (const auto milliseconds : { 0.5f, 2.0f, 5.0f })
    {
        LoadedSlot loaded { makeSpike() };

        IrSlot::Levels levels;
        levels.alignMs = milliseconds;
        loaded.settle (levels);

        const auto output = impulseThrough (loaded.slot);
        const auto expected = juce::roundToInt ((double) milliseconds * 0.001 * rate);

        INFO (milliseconds << " ms should be " << expected << " samples");
        CHECK (peakPosition (output) == expected);
    }
}

TEST_CASE ("Alignment resolves finer than a sample", "[dsp][slot][align]")
{
    // The whole reason the control reads to two decimals. Half a sample at 48 kHz is
    // ten microseconds, which is well inside the range two microphones on one cabinet
    // disagree by -- and a shift that rounded to the nearest sample would leave that
    // impossible to dial out.
    //
    // Measured as where the impulse's centre sits rather than as how it is split
    // between two samples. A third-order interpolator spreads it over four taps with a
    // kernel of its own shape, so the split is not the even pair a linear read would
    // give -- but the centre is the arrival time, which is the thing the control sets
    // and the thing the ear hears.
    const auto centroid = [] (float milliseconds)
    {
        LoadedSlot loaded { makeSpike() };

        IrSlot::Levels levels;
        levels.alignMs = milliseconds;
        loaded.settle (levels);

        const auto output = impulseThrough (loaded.slot);

        // The first moment of the amplitudes, not of their squares. A Lagrange kernel
        // has negative lobes, and squaring them throws away the sign that makes them
        // pull the centre back where it belongs -- which is exactly the arithmetic
        // that makes this a fractional delay rather than a smear.
        auto weighted = 0.0, total = 0.0;

        for (int i = 0; i < 32; ++i)
        {
            const auto amplitude = (double) output.getSample (0, i);

            weighted += amplitude * i;
            total += amplitude;
        }

        return std::abs (total) > 1.0e-9 ? weighted / total : 0.0;
    };

    const auto perSample = (float) (1000.0 / rate);

    const auto none = centroid (0.0f);
    const auto half = centroid (perSample * 0.5f);
    const auto whole = centroid (perSample);

    INFO ("centres: " << none << ", " << half << ", " << whole);

    CHECK (half > none);
    CHECK (whole > half);
    CHECK_THAT (half - none, Catch::Matchers::WithinAbs (0.5, 0.2));
}

TEST_CASE ("Alignment at rest leaves the response alone", "[dsp][slot][align]")
{
    // Bit for bit, not near enough. A delay line reading a fractional sample is an
    // interpolation, and one that ran at rest would put a quiet low-pass on every
    // cabinet whether or not anybody had touched the control.
    LoadedSlot loaded { makeSpike() };
    loaded.settle ({});

    const auto output = impulseThrough (loaded.slot);

    CHECK (peakPosition (output) == 0);
    CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (1.0f, 2.0e-3f));

    for (int i = 1; i < 16; ++i)
    {
        INFO ("sample " << i);
        CHECK_THAT (output.getSample (0, i), Catch::Matchers::WithinAbs (0.0f, 1.0e-4f));
    }
}

TEST_CASE ("A response recorded at another rate is brought to the host's", "[dsp][slot]")
{
    // A capture is a recording of a speaker. Convolved at the wrong rate it is the
    // same speaker transposed, which is audible as the cabinet having changed size.
    juce::AudioBuffer<float> response (1, 2000);
    response.clear();

    // A spike a known distance in, so the check is where it lands rather than how it
    // sounds: at half the rate, a spike 1000 samples in should arrive 2000 in.
    response.setSample (0, 1000, 1.0f);

    LoadedSlot loaded { response, rate * 0.5 };
    loaded.settle ({});

    const auto output = impulseThrough (loaded.slot);

    INFO ("peak at " << peakPosition (output));
    CHECK (std::abs (peakPosition (output) - 2000) <= 2);
}

TEST_CASE ("Unloading empties the slot without tearing the engine down", "[dsp][slot]")
{
    LoadedSlot loaded { makeSpike() };
    loaded.settle ({});

    REQUIRE (loaded.slot.isActive());

    loaded.slot.unload();

    CHECK_FALSE (loaded.slot.isLoaded());
    CHECK_FALSE (loaded.slot.isActive());

    // And it can be filled again.
    REQUIRE (loaded.slot.loadFrom (loaded.file).wasOk());
    CHECK (loaded.slot.isActive());
}

TEST_CASE ("The measured spectrum covers the audible range in order", "[dsp][slot]")
{
    // The grid the graph plots against. Getting it backwards or off by an octave would
    // draw four cabinets that all look wrong in the same way, which is the hardest kind
    // of wrong to notice.
    CHECK_THAT (IrSlot::spectrumFrequency (0),
                Catch::Matchers::WithinRel (IrSlot::spectrumLowHz, 1.0e-4f));

    CHECK_THAT (IrSlot::spectrumFrequency (IrSlot::spectrumPoints - 1),
                Catch::Matchers::WithinRel (IrSlot::spectrumHighHz, 1.0e-4f));

    // Logarithmic: the middle of the grid is the geometric mean of its ends, not the
    // arithmetic one.
    const auto middle = IrSlot::spectrumFrequency (IrSlot::spectrumPoints / 2);
    CHECK (middle > 500.0f);
    CHECK (middle < 900.0f);
}

TEST_CASE ("A loaded slot measures a spectrum, an empty one does not", "[dsp][slot]")
{
    juce::AudioBuffer<float> response (1, 1024);
    juce::Random random { 3 };

    for (int i = 0; i < response.getNumSamples(); ++i)
        response.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * 0.4f);

    LoadedSlot loaded { response };

    const auto& spectrum = loaded.slot.getSpectrumDb();
    REQUIRE (spectrum.size() == (size_t) IrSlot::spectrumPoints);

    auto anyMeasured = false;

    for (auto level : spectrum)
    {
        REQUIRE (std::isfinite (level));
        anyMeasured |= level > -100.0f;
    }

    CHECK (anyMeasured);
}

TEST_CASE ("Resolution decides how much of a response is convolved", "[dsp][slot]")
{
    // A response longer than the setting is dropped past it, which is what makes the
    // shortest setting cheaper rather than merely quieter: the engine's cost is
    // proportional to what it is given.
    // Longer than HiRes keeps, so both settings are a real cut rather than the file
    // running out first.
    juce::AudioBuffer<float> response (1, Parameters::resolutions.back() + 4000);
    response.clear();

    response.setSample (0, 0, 1.0f);
    response.setSample (0, 1000, 0.8f);
    response.setSample (0, response.getNumSamples() - 100, 0.8f);

    LoadedSlot loaded { response };

    const auto reaches = [&loaded] (double seconds)
    {
        loaded.slot.setShaping ({ seconds });
        loaded.settle ({});

        return loaded.slot.getShapedResponse().getNumSamples();
    };

    // Read off the settings themselves, so changing either cannot leave this quietly
    // asserting against a number the plugin no longer uses.
    const auto smallest = Parameters::resolutions.front();
    const auto biggest = Parameters::resolutions.back();

    const auto lowest = reaches (Parameters::responseSeconds (0));
    const auto highest = reaches (Parameters::responseSeconds ((int) Parameters::resolutions.size() - 1));

    INFO (smallest << " kept " << lowest << ", " << biggest << " kept " << highest);

    CHECK (lowest <= smallest);
    CHECK (highest > lowest);
    CHECK (highest <= biggest);
}

TEST_CASE ("A truncated response ends on silence", "[dsp][slot]")
{
    // Cut off rather than faded, a response ends on whatever sample it happened to
    // reach -- and a step at the end of a filter is convolved into every transient
    // that goes through it, which is heard as a click on the attack rather than as a
    // shortened tail.
    juce::AudioBuffer<float> response (1, 12000);

    for (int i = 0; i < response.getNumSamples(); ++i)
        response.setSample (0, i, i == 0 ? 1.0f : 0.5f);   // no decay at all

    LoadedSlot loaded { response };
    loaded.slot.setShaping ({ Parameters::responseSeconds (0) });
    loaded.settle ({});

    const auto& shaped = loaded.slot.getShapedResponse();
    const auto count = shaped.getNumSamples();

    REQUIRE (count > 16);

    const auto last = std::abs (shaped.getSample (0, count - 1));
    const auto middle = std::abs (shaped.getSample (0, count / 2));

    INFO ("ends at " << last << ", middle is " << middle);
    CHECK (last < middle * 0.05f);
}
