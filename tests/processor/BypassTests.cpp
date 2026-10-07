#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>

using namespace Cabinets;

/*
    Bypass is pressed from two places -- the plugin's own button and the host's -- and
    has to be one switch whichever is used. And because it is pressed with the track
    running, in the middle of an A/B, switching it must not be heard as anything but
    the cabinets going away and coming back.
*/

TEST_CASE ("The host's bypass is the plugin's own", "[processor][bypass]")
{
    // Without this the host makes a bypass of its own, which neither moves the button
    // on the toolbar nor goes through the crossfade below: two switches that disagree
    // about whether the plugin is on.
    PluginProcessor plugin;
    CHECK (plugin.getBypassParameter() == plugin.getAPVTS().getParameter (ParamID::bypass));
}

namespace
{
    /** A steady tone through the plugin with the bypass toggled every `period` blocks,
        returning the worst break in the waveform relative to the tone's own.

        The second difference, as in ArtefactTests: it is near zero for any smooth
        signal and a spike wherever the waveform actually steps. The baseline is the
        louder of the two settled states, bypassed and not -- with the output trimmed
        they are at different levels, and a fade up to the louder one is not a click. */
    float worstToggleRatio (PluginProcessor& plugin)
    {
        constexpr int toneBlock = 64;
        constexpr int period = 60;
        constexpr int warmUp = 2 * period;

        juce::AudioBuffer<float> buffer (2, toneBlock);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double phase = 0.0;
        const auto advance = juce::MathConstants<double>::twoPi * 220.0 / rate;

        // Bypassed for the first period and on for the second, each settled by its end.
        auto bypassed = true;
        setParameter (plugin, ParamID::bypass, 1.0f);

        for (int block = 0; block < warmUp + period * 8; ++block)
        {
            if (block >= period && block % period == 0)
            {
                bypassed = ! bypassed;
                setParameter (plugin, ParamID::bypass, bypassed ? 1.0f : 0.0f);
            }

            for (int i = 0; i < toneBlock; ++i)
            {
                const auto sample = (float) std::sin (phase);
                phase += advance;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < toneBlock; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        const auto worstBetween = [&out] (int from, int to)
        {
            auto worst = 0.0f;

            for (int i = juce::jmax (2, from); i < to; ++i)
                worst = juce::jmax (worst, std::abs (out[(size_t) i] - 2.0f * out[(size_t) i - 1]
                                                     + out[(size_t) i - 2]));

            return worst;
        };

        const auto settledEnd = [] (int periodIndex)
        {
            return std::pair { (periodIndex * period + period / 2) * toneBlock,
                               (periodIndex + 1) * period * toneBlock };
        };

        const auto [bypassedFrom, bypassedTo] = settledEnd (0);
        const auto [onFrom, onTo] = settledEnd (1);

        const auto baseline = juce::jmax (worstBetween (bypassedFrom, bypassedTo),
                                          worstBetween (onFrom, onTo));
        const auto toggling = worstBetween (warmUp * toneBlock, (int) out.size());

        INFO ("baseline " << baseline << ", toggling " << toggling);
        REQUIRE (baseline > 0.0f);

        return toggling / baseline;
    }
}

TEST_CASE ("Toggling the bypass does not click", "[processor][bypass][clicks]")
{
    // A cabinet ten samples late against the dry signal, so the two are out of step by
    // a third of a radian at 220 Hz: what any real cabinet does to the phase, and enough
    // that swapping one for the other in a sample is a step in the waveform.
    SECTION ("a cabinet")
    {
        FourCabinets set;
        REQUIRE (set.plugin.loadImpulseResponse (1, set.files[1]).wasOk());

        CHECK (worstToggleRatio (set.plugin) < 3.0f);
    }

    // The output trim is part of what bypass takes away. It used to be ramped back to
    // unity over fifty milliseconds while the cabinet left in one sample, so for those
    // fifty you heard one without the other.
    SECTION ("a cabinet with the output trimmed")
    {
        FourCabinets set;
        REQUIRE (set.plugin.loadImpulseResponse (1, set.files[1]).wasOk());
        setParameter (set.plugin, ParamID::outputGain, -9.0f);

        CHECK (worstToggleRatio (set.plugin) < 3.0f);
    }

    // Nothing loaded is the input through the trim, and bypass is the input without it.
    SECTION ("nothing loaded, output trimmed")
    {
        FourCabinets set;
        setParameter (set.plugin, ParamID::outputGain, 9.0f);

        CHECK (worstToggleRatio (set.plugin) < 3.0f);
    }
}

TEST_CASE ("Coming out of bypass does not play what was heard before it", "[processor][bypass][clicks]")
{
    // The same ghost as a slot filled again: a cabinet that is not fed while bypassed
    // keeps the input history it had when bypass was pressed, and convolves it on the
    // way back in -- the past, arriving on a signal that has since gone silent. On a
    // response with a room on it that is a second of something nobody is playing.
    FourCabinets set;
    REQUIRE (set.plugin.loadImpulseResponse (1, set.files[1]).wasOk());

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 23 };

    for (int block = 0; block < 40; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

        set.plugin.processBlock (buffer, midi);
    }

    setParameter (set.plugin, ParamID::bypass, 1.0f);

    // Bypassed long enough for the fade to land, and silent from the moment it was pressed.
    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        set.plugin.processBlock (buffer, midi);
    }

    setParameter (set.plugin, ParamID::bypass, 0.0f);

    auto ghost = 0.0f;

    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        set.plugin.processBlock (buffer, midi);
        ghost = juce::jmax (ghost, buffer.getMagnitude (0, blockSize));
    }

    INFO ("loudest sample in silence after leaving bypass: " << ghost);
    CHECK (ghost == 0.0f);
}
