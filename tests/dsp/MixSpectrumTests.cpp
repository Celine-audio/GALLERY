#include <dsp/MixSpectrum.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

/*
    The blended curve. The thing worth protecting here is that the four responses are
    added as *waveforms* -- a sum of their magnitude curves would be simpler, faster,
    and would draw a cabinet blend with none of the notches a real one has.
*/

namespace
{
    constexpr double rate = 48000.0;

    juce::AudioBuffer<float> spikeAt (int position, float amplitude = 1.0f)
    {
        juce::AudioBuffer<float> buffer (1, 2048);
        buffer.clear();
        buffer.setSample (0, position, amplitude);

        return buffer;
    }

    /** Everything off, so the cuts do nothing and what is measured is the sum alone. */
    void give (MixSpectrum& mix, int slot, const juce::AudioBuffer<float>& response)
    {
        mix.setResponse (slot, 1, response, CutFilter::lowestHz, 12, CutFilter::highestHz, 12);
    }

    Parameters::BlendWeights evenPair()
    {
        return { 0.5f, 0.5f, 0.0f, 0.0f };
    }
}

//==============================================================================
TEST_CASE ("Two cabinets in opposite polarity cancel", "[dsp][mix]")
{
    // The plainest case there is, and the one a sum of magnitude curves gets exactly
    // backwards: two identical responses, one inverted, are silence. Added as curves
    // they would come out at *twice* the level of either.
    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (64));
    give (mix, 1, spikeAt (64));

    std::vector<float> curve;
    REQUIRE (mix.compute (evenPair(), { false, true, false, false }, {}, curve));

    REQUIRE (curve.size() == (size_t) LogSpectrum::points);

    for (size_t i = 0; i < curve.size(); ++i)
    {
        INFO ("point " << i << " at " << LogSpectrum::frequencyForPoint ((int) i) << " Hz");
        CHECK (curve[i] < -100.0f);
    }
}

TEST_CASE ("Two cabinets a little apart in time comb", "[dsp][mix]")
{
    // What the Align control exists for. Two copies of one capture separated by n
    // samples cancel wherever half a wavelength fits the gap: at 48 kHz and 48 samples
    // apart, the first null is at 500 Hz and they repeat every kilohertz after it.
    //
    // A sum of magnitudes has no way to produce a null at all, so this is the test that
    // fails if the summing is ever moved into the magnitude domain.
    constexpr int offset = 48;

    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (64));
    give (mix, 1, spikeAt (64 + offset));

    std::vector<float> curve;
    REQUIRE (mix.compute (evenPair(), {}, {}, curve));

    const auto levelAt = [&curve] (float hz)
    {
        auto best = 0;

        for (int i = 1; i < LogSpectrum::points; ++i)
            if (std::abs (LogSpectrum::frequencyForPoint (i) - hz)
                    < std::abs (LogSpectrum::frequencyForPoint (best) - hz))
                best = i;

        return curve[(size_t) best];
    };

    // Nulls where the two disagree, peaks where they agree.
    const auto firstNull = levelAt (rate / (2.0f * offset));           // 500 Hz
    const auto firstPeak = levelAt (rate / (float) offset);            // 1 kHz

    INFO ("null " << firstNull << " dB, peak " << firstPeak << " dB");
    CHECK (firstPeak - firstNull > 20.0f);
}

TEST_CASE ("Alignment puts a cabinet exactly where the delay line does", "[dsp][mix]")
{
    // A spike at zero pushed back N samples has to land on top of a spike already at N.
    // Given opposite polarity the two are then silence, and nothing but an exact offset
    // produces that -- where a comb test at the right frequency passes happily with the
    // whole cabinet a sample early.
    constexpr int offset = 40;

    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (0));
    give (mix, 1, spikeAt (offset));

    std::vector<float> curve;
    REQUIRE (mix.compute (evenPair(), { false, true, false, false },
                          { (double) offset / rate, 0.0, 0.0, 0.0 }, curve));

    for (size_t i = 0; i < curve.size(); ++i)
    {
        INFO ("point " << i << " at " << LogSpectrum::frequencyForPoint ((int) i) << " Hz");
        CHECK (curve[i] < -100.0f);
    }

    // And half a sample off is not silence, so the check above is not passing because
    // the delay is being ignored altogether.
    std::vector<float> nearly;
    REQUIRE (mix.compute (evenPair(), { false, true, false, false },
                          { ((double) offset + 0.5) / rate, 0.0, 0.0, 0.0 }, nearly));

    auto loudest = -200.0f;

    for (const auto level : nearly)
        loudest = juce::jmax (loudest, level);

    INFO ("loudest point at half a sample out: " << loudest << " dB");
    CHECK (loudest > -40.0f);
}

TEST_CASE ("Alignment combs the mix curve", "[dsp][mix]")
{
    // Align is a delay line on the audio thread rather than a shift baked into the
    // response, so it is not in the samples this curve is built from. Summed at offset
    // zero the picture is the same at every setting of the knob -- which is the one
    // picture that makes the control look like it does nothing.
    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (64));
    give (mix, 1, spikeAt (64));

    const auto pointNear = [] (double hz)
    {
        auto best = 0;

        for (int i = 1; i < LogSpectrum::points; ++i)
            if (std::abs (LogSpectrum::frequencyForPoint (i) - hz)
                    < std::abs (LogSpectrum::frequencyForPoint (best) - hz))
                best = i;

        return (size_t) best;
    };

    const auto curveFor = [&mix] (double delaySamples)
    {
        std::vector<float> curve;
        REQUIRE (mix.compute (evenPair(), {}, { 0.0, delaySamples / rate, 0.0, 0.0 }, curve));

        return curve;
    };

    const auto biggestGap = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        auto worst = 0.0f;

        for (size_t i = 0; i < a.size(); ++i)
            worst = juce::jmax (worst, std::abs (a[i] - b[i]));

        return worst;
    };

    const auto together = curveFor (0.0);
    const auto delayed = curveFor (48.0);

    // Forty-eight samples at 48 kHz: the first null at 500 Hz, the first peak at one
    // kilohertz, exactly as if the second capture had been recorded that much later.
    const auto atNull = pointNear (rate / 96.0);
    const auto atPeak = pointNear (rate / 48.0);

    INFO ("aligned " << together[atNull] << " dB, delayed " << delayed[atNull] << " dB");
    CHECK (together[atNull] - delayed[atNull] > 20.0f);
    CHECK (delayed[atPeak] - delayed[atNull] > 20.0f);

    // And half a sample, which is where a shift rounded to whole ones gives itself away:
    // the curve would be identical to one of its neighbours, and the notches would jump
    // from one place to the next as the knob turned instead of sliding.
    const auto fractional = curveFor (48.5);

    CHECK (biggestGap (fractional, delayed) > 1.0f);
    CHECK (biggestGap (fractional, curveFor (49.0)) > 1.0f);
}

TEST_CASE ("One cabinet alone is that cabinet", "[dsp][mix]")
{
    // A blend pointing entirely at one corner has to give back the response that is in
    // it, unchanged -- otherwise the mix curve and the split curves disagree at exactly
    // the setting where they must be the same picture.
    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (64));
    give (mix, 1, spikeAt (300));

    std::vector<float> alone;
    REQUIRE (mix.compute ({ 1.0f, 0.0f, 0.0f, 0.0f }, {}, {}, alone));

    // A single spike is flat across the spectrum, whatever its position.
    for (size_t i = 0; i < alone.size(); ++i)
    {
        INFO ("point " << i);
        CHECK_THAT (alone[i], Catch::Matchers::WithinAbs (alone[0], 0.5f));
    }
}

TEST_CASE ("Nothing loaded is no curve rather than a flat one", "[dsp][mix]")
{
    // A curve along the bottom of the graph would be a claim about a blend that does
    // not exist, and it would be drawn.
    MixSpectrum mix;
    mix.prepare (rate);

    std::vector<float> curve;
    CHECK_FALSE (mix.compute ({ 0.25f, 0.25f, 0.25f, 0.25f }, {}, {}, curve));
}

TEST_CASE ("A cut moves the mix curve", "[dsp][mix]")
{
    // The cuts are not baked into the response the audio thread convolves, so the mix
    // display has to apply its own copies of them. If it stopped, the curve would go on
    // showing a cabinet whose bottom end had been removed.
    MixSpectrum mix;
    mix.prepare (rate);

    give (mix, 0, spikeAt (64));

    std::vector<float> flat;
    REQUIRE (mix.compute ({ 1.0f, 0.0f, 0.0f, 0.0f }, {}, {}, flat));

    mix.setResponse (0, 1, spikeAt (64), 500.0f, 24, CutFilter::highestHz, 12);

    std::vector<float> cut;
    REQUIRE (mix.compute ({ 1.0f, 0.0f, 0.0f, 0.0f }, {}, {}, cut));

    // Well into the stopband of a 500 Hz, 24 dB/octave low cut.
    const auto index = [] (float hz)
    {
        auto best = 0;

        for (int i = 1; i < LogSpectrum::points; ++i)
            if (std::abs (LogSpectrum::frequencyForPoint (i) - hz)
                    < std::abs (LogSpectrum::frequencyForPoint (best) - hz))
                best = i;

        return (size_t) best;
    };

    const auto low = index (60.0f);

    INFO ("uncut " << flat[low] << " dB, cut " << cut[low] << " dB");
    CHECK (flat[low] - cut[low] > 40.0f);
}
