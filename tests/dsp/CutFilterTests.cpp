#include <Parameters.h>
#include <dsp/CutFilter.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

/*
    The cut filters, checked against the slope they claim.

    Worth testing properly rather than by ear: a cascade of biquads at the wrong Q is
    still a filter that cuts, and still sounds broadly like the control says. What it
    is not is Butterworth -- it has a resonant peak at the corner, or it sags before
    it -- and on four cabinets at once that reads as "the low cut sounds odd" rather
    than as a specific mistake.
*/

namespace
{
    constexpr double rate = 48000.0;

    /** The response measured rather than derived: a sine at `frequency` pushed through
        the filter, and the amplitude that comes out.

        Deliberately not a check of magnitudeAt against itself. That function exists so
        the graph can draw what the audio is doing, and it earns its place only if it
        agrees with what the audio actually does -- which is what this measures. */
    float measure (CutFilter& filter, double frequency)
    {
        constexpr int settle = 40000;
        constexpr int window = 20000;

        juce::AudioBuffer<float> buffer (1, settle + window);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, (float) std::sin (juce::MathConstants<double>::twoPi
                                                      * frequency * (double) i / rate));

        filter.reset();
        filter.process (buffer);

        auto peak = 0.0f;

        for (int i = settle; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));

        return peak;
    }

    float decibels (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }
}

//==============================================================================
TEST_CASE ("A cut at the end of its travel is exactly out of the way", "[dsp][cut]")
{
    // Not nearly out of the way. A filter left running flat at the end of its range
    // still costs arithmetic and still shifts phase, and on the low cut it would do
    // that on all four slots by default -- which is a plugin that alters the signal
    // before anybody has touched it.
    CutFilter low { CutFilter::Kind::lowCut };
    low.prepare (rate, 1);
    low.setParameters (CutFilter::lowestHz, 12);

    CHECK_FALSE (low.isActive());
    CHECK (low.magnitudeAt (1000.0f) == 1.0f);

    CutFilter high { CutFilter::Kind::highCut };
    high.prepare (rate, 1);
    high.setParameters (CutFilter::highestHz, 12);

    CHECK_FALSE (high.isActive());
    CHECK (high.magnitudeAt (1000.0f) == 1.0f);
}

TEST_CASE ("Each slope falls at the rate it advertises", "[dsp][cut]")
{
    // Measured two octaves into the stopband, where the asymptote dominates. Closer to
    // the corner a Butterworth is still turning the bend, and testing there would be
    // testing the bend rather than the slope.
    for (const auto slope : Parameters::slopes)
    {
        CutFilter filter { CutFilter::Kind::lowCut };
        filter.prepare (rate, 1);
        filter.setParameters (1000.0f, slope);

        REQUIRE (filter.isActive());

        const auto atTwoOctaves = decibels (measure (filter, 250.0));
        const auto atThreeOctaves = decibels (measure (filter, 125.0));

        // One more octave down is one more slope's worth of attenuation.
        const auto perOctave = atTwoOctaves - atThreeOctaves;

        INFO ("slope: " << slope << " dB/oct, measured " << perOctave);
        CHECK_THAT (perOctave, Catch::Matchers::WithinAbs ((float) slope, 1.0f));
    }
}

TEST_CASE ("A high cut falls at its slope too", "[dsp][cut]")
{
    // Measured far below Nyquist, and that is not fussiness. A digital low-pass has a
    // zero *at* Nyquist, so its response falls away faster than the analogue slope as
    // it approaches -- a 12 dB/oct cut genuinely measures nearer 14 dB/oct an octave
    // or two below 24 kHz. That is the bilinear transform doing what it does rather
    // than a mistake in the cascade, so the test asks the question where the
    // asymptote is the thing that answers it.
    for (const auto slope : Parameters::slopes)
    {
        CutFilter filter { CutFilter::Kind::highCut };
        filter.prepare (rate, 1);
        filter.setParameters (200.0f, slope);

        const auto atTwoOctaves = decibels (measure (filter, 800.0));
        const auto atThreeOctaves = decibels (measure (filter, 1600.0));
        const auto perOctave = atTwoOctaves - atThreeOctaves;

        INFO ("slope: " << slope << " dB/oct, measured " << perOctave);
        CHECK_THAT (perOctave, Catch::Matchers::WithinAbs ((float) slope, 1.0f));
    }
}

TEST_CASE ("The passband is left alone", "[dsp][cut]")
{
    // A cascade whose sections are individually normalised but collectively wrong
    // shows up here as a passband that is not unity.
    CutFilter filter { CutFilter::Kind::lowCut };
    filter.prepare (rate, 1);
    filter.setParameters (100.0f, 24);

    CHECK_THAT (decibels (measure (filter, 4000.0)), Catch::Matchers::WithinAbs (0.0f, 0.2f));
}

TEST_CASE ("The drawn curve is the filter that is running", "[dsp][cut]")
{
    // The graph asks magnitudeAt what shape to draw. If it disagreed with the audio,
    // the display would be confidently wrong -- which is worse than having no display,
    // because it would be trusted.
    for (const auto kind : { CutFilter::Kind::lowCut, CutFilter::Kind::highCut })
    {
        CutFilter filter { kind };
        filter.prepare (rate, 1);
        filter.setParameters (800.0f, 18);

        for (const auto frequency : { 100.0, 400.0, 800.0, 1600.0, 6000.0 })
        {
            const auto measured = decibels (measure (filter, frequency));
            const auto drawn = decibels (filter.magnitudeAt ((float) frequency));

            INFO ("at " << frequency << " Hz: measured " << measured << ", drawn " << drawn);
            CHECK_THAT (drawn, Catch::Matchers::WithinAbs (measured, 0.5f));
        }
    }
}

TEST_CASE ("Butterworth section Qs are the ones the tables give", "[dsp][cut]")
{
    // The single line of arithmetic that makes a stack of biquads a Butterworth rather
    // than several unrelated filters. Written down here because these four numbers are
    // checkable against any filter table, and nothing else in the cascade is.
    CHECK_THAT (CutFilter::sectionQ (2, 0), Catch::Matchers::WithinAbs (0.7071, 1.0e-3));
    CHECK_THAT (CutFilter::sectionQ (3, 0), Catch::Matchers::WithinAbs (1.0, 1.0e-3));
    CHECK_THAT (CutFilter::sectionQ (4, 0), Catch::Matchers::WithinAbs (1.3066, 1.0e-3));
    CHECK_THAT (CutFilter::sectionQ (4, 1), Catch::Matchers::WithinAbs (0.5412, 1.0e-3));
}

TEST_CASE ("Switching a filter off does not store a click for later", "[dsp][cut]")
{
    // A cut swept to the end of its travel stops filtering, and process() then returns
    // without touching the state words. Left as they were, they are a snapshot of a
    // loud signal waiting for the next time the control moves back off the end -- at
    // which point they arrive all at once, into whatever is playing then.
    CutFilter filter { CutFilter::Kind::lowCut };
    filter.prepare (rate, 1);
    filter.setParameters (500.0f, 24);

    juce::AudioBuffer<float> buffer (1, 512);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, i % 2 == 0 ? 1.0f : -1.0f);

    filter.process (buffer);

    // Swept to the end of its travel and left there for a moment, which is what
    // happens in use -- the plugin goes on processing while a control sits still.
    filter.setParameters (CutFilter::lowestHz, 24);

    buffer.clear();
    filter.process (buffer);

    filter.setParameters (500.0f, 24);                // and back on

    buffer.clear();
    filter.process (buffer);

    CHECK (buffer.getMagnitude (0, buffer.getNumSamples()) == 0.0f);
}
