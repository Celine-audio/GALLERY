#include <dsp/LogSpectrum.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

/*
    Reading a linear-frequency transform onto a logarithmic axis.

    The failure this guards against is specific and was visible for three rounds of
    review before it was understood: near the bottom of the axis a dozen consecutive
    display points fall inside one transform bin, so reading the nearest bin -- or
    averaging a range of them rounded out to whole bins -- gives all twelve the same
    value. The curve comes out as flat treads joined by steps, and because the fault is
    in the data no amount of smoothing or curve fitting touches it.

    So the property to test is not "the curve looks nice". It is that **no two
    neighbouring points share a value unless the spectrum really is flat there**, which
    is what a staircase violates and a line does not.
*/

namespace
{
    constexpr double rate = 48000.0;

    /** A magnitude spectrum that slopes steadily across the whole range, so that any
        flat run in the result is the reader's doing rather than the signal's. */
    std::vector<float> slopingSpectrum (int numBins)
    {
        std::vector<float> magnitudes ((size_t) numBins);

        for (int bin = 0; bin < numBins; ++bin)
            magnitudes[(size_t) bin] = 1.0f / (1.0f + (float) bin * 0.01f);

        return magnitudes;
    }

    /** The longest run of consecutive points holding the same value. */
    int longestFlatRun (const std::vector<float>& curve)
    {
        auto longest = 1;
        auto run = 1;

        for (size_t i = 1; i < curve.size(); ++i)
        {
            if (juce::approximatelyEqual (curve[i], curve[i - 1]))
                ++run;
            else
                run = 1;

            longest = juce::jmax (longest, run);
        }

        return longest;
    }
}

//==============================================================================
TEST_CASE ("The grid spans the audible range logarithmically", "[dsp][spectrum]")
{
    CHECK_THAT (LogSpectrum::frequencyForPoint (0),
                Catch::Matchers::WithinRel (LogSpectrum::lowestHz, 1.0e-4f));

    CHECK_THAT (LogSpectrum::frequencyForPoint (LogSpectrum::points - 1),
                Catch::Matchers::WithinRel (LogSpectrum::highestHz, 1.0e-4f));

    // The middle of a logarithmic axis is the geometric mean of its ends, not the
    // arithmetic one -- 632 Hz rather than 10 kHz.
    const auto middle = LogSpectrum::frequencyForPoint (LogSpectrum::points / 2);

    CHECK (middle > 550.0f);
    CHECK (middle < 750.0f);
}

TEST_CASE ("A sloping spectrum is read as a slope, not as a staircase", "[dsp][spectrum]")
{
    // The regression test proper. At 0.73 Hz bins the bottom of the axis steps by a
    // fraction of a bin per point, which is exactly where reading the nearest bin
    // produces treads a dozen points long.
    const auto size = LogSpectrum::fftSizeFor (4800);
    const auto bins = size / 2 + 1;
    const auto magnitudes = slopingSpectrum (bins);

    std::vector<float> curve;
    LogSpectrum::sample (magnitudes.data(), bins, rate / (double) size, curve);

    REQUIRE (curve.size() == (size_t) LogSpectrum::points);

    INFO ("longest flat run: " << longestFlatRun (curve) << " points");
    CHECK (longestFlatRun (curve) <= 2);

    // ...and it really is a slope: monotonically down, since the spectrum it read was.
    for (size_t i = 1; i < curve.size(); ++i)
    {
        INFO ("point " << i);
        REQUIRE (curve[i] <= curve[i - 1] + 1.0e-3f);
    }
}

TEST_CASE ("The bottom of the axis is where the staircase used to be", "[dsp][spectrum]")
{
    // Stated separately because it is the half that matters: the top of a logarithmic
    // axis has many bins to a point and was never the problem, so a test averaged over
    // the whole range can pass while the bottom two octaves are still stepped.
    const auto size = LogSpectrum::fftSizeFor (4800);
    const auto bins = size / 2 + 1;
    const auto magnitudes = slopingSpectrum (bins);

    std::vector<float> curve;
    LogSpectrum::sample (magnitudes.data(), bins, rate / (double) size, curve);

    // Twenty hertz to eighty: the first fifth of the width, and the part where a bin is
    // several display points wide.
    std::vector<float> bottom;

    for (int point = 0; point < LogSpectrum::points; ++point)
    {
        if (LogSpectrum::frequencyForPoint (point) > 80.0f)
            break;

        bottom.push_back (curve[(size_t) point]);
    }

    REQUIRE (bottom.size() > 100);

    INFO ("longest flat run below 80 Hz: " << longestFlatRun (bottom) << " points");
    CHECK (longestFlatRun (bottom) <= 2);
}

TEST_CASE ("Padding a short response samples its spectrum densely", "[dsp][spectrum]")
{
    // A cabinet capture is short, and it is short captures whose spectra are sampled
    // most coarsely by a transform of their own length. The padding is what turns a
    // handful of bins across the bottom octaves into enough to draw through.
    CHECK (LogSpectrum::fftSizeFor (512) >= LogSpectrum::minimumFftSize);
    CHECK (LogSpectrum::fftSizeFor (4800) >= 4800 * 8);
    CHECK (LogSpectrum::fftSizeFor (1 << 20) == LogSpectrum::maximumFftSize);
}

TEST_CASE ("A flat spectrum reads flat", "[dsp][spectrum]")
{
    // The other direction: the reader must not invent detail where there is none.
    const auto size = LogSpectrum::fftSizeFor (4800);
    const auto bins = size / 2 + 1;

    std::vector<float> magnitudes ((size_t) bins, 0.25f);
    std::vector<float> curve;

    LogSpectrum::sample (magnitudes.data(), bins, rate / (double) size, curve);

    const auto expected = (float) (20.0 * std::log10 (0.25));

    for (size_t i = 0; i < curve.size(); ++i)
    {
        INFO ("point " << i);
        REQUIRE_THAT (curve[i], Catch::Matchers::WithinAbs (expected, 0.05f));
    }
}
