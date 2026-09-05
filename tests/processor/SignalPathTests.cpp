#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

/*
    What the plugin does with the signal as a whole: whether the cabinets are all in it,
    what happens when there are none, and what the output measurement costs when nobody
    is looking at it.
*/

TEST_CASE ("The four cabinets are all in the sum", "[processor]")
{
    // Spikes at four different places, so each cabinet's contribution can be found
    // separately: a sum that dropped one, or ran one twice, shows up as a missing or
    // doubled spike rather than as a total that is merely wrong.
    FourCabinets set;
    set.load (4);

    const auto output = impulseThrough (set.plugin);

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        INFO ("cabinet " << slot << " at sample " << slot * 10);
        CHECK_THAT (output.getSample (0, slot * 10), Catch::Matchers::WithinAbs (0.25f, 0.03f));
    }
}

TEST_CASE ("With nothing loaded the plugin is transparent", "[processor]")
{
    // Deliberate, and the opposite of what the arithmetic would give: the plugin
    // replaces the signal with what comes out of the cabinets, so an empty set of them
    // is silence. A plugin that mutes a track the moment it is inserted gets removed
    // before anybody finds the Load button.
    PluginProcessor plugin;
    plugin.prepareToPlay (rate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize), original (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 12 };

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

    original.makeCopyOf (buffer);
    plugin.processBlock (buffer, midi);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (original.getSample (0, i), 1.0e-5f));
}

TEST_CASE ("A loaded cabinet replaces the signal rather than adding to it", "[processor]")
{
    // The other side of the transparency rule: once there is a cabinet, what comes out
    // is the cabinet. A dry path left in by accident would sound like a cabinet mixed
    // with a DI, which is a real sound and therefore easy to fail to notice.
    FourCabinets set;
    set.load (2);

    const auto output = impulseThrough (set.plugin);

    // Two spikes, at the two cabinets' positions, and nothing where the dry signal
    // would have been if it were still there. The first cabinet's spike is at zero, so
    // the check that the dry is gone is that it is at a half rather than at one and a
    // half.
    CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (0.5f, 0.03f));
    CHECK_THAT (output.getSample (0, 10), Catch::Matchers::WithinAbs (0.5f, 0.03f));
    CHECK_THAT (output.getSample (0, 5), Catch::Matchers::WithinAbs (0.0f, 0.03f));
}

TEST_CASE ("The output analyser measures only when it is asked to", "[processor]")
{
    // It is the one thing here that costs the audio thread something nobody has to
    // spend, so "off" has to mean off rather than measured-and-ignored.
    FourCabinets set;
    set.load (1);
    set.plugin.setUiActive (true);

    std::vector<float> spectrum;

    CHECK_FALSE (set.plugin.isOutputAnalysisEnabled());
    CHECK_FALSE (set.plugin.copyOutputSpectrumDb (spectrum));

    set.plugin.setOutputAnalysisEnabled (true);
    CHECK (set.plugin.isOutputAnalysisEnabled());

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 33 };

    // Enough to fill several transform frames. A frame cannot be measured until it has
    // been filled, so this has to outlast the analyser's window -- which is what makes
    // the number here a fact about the FFT size rather than an arbitrary "enough".
    const auto blocksPerFrame = (1 << spectrumFftOrder) / blockSize;

    for (int block = 0; block < blocksPerFrame * 4; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

        set.plugin.processBlock (buffer, midi);
    }

    REQUIRE (set.plugin.copyOutputSpectrumDb (spectrum));
    REQUIRE (spectrum.size() == (size_t) IrSlot::spectrumPoints);

    auto measured = false;

    for (auto level : spectrum)
    {
        REQUIRE (std::isfinite (level));
        measured |= level > -100.0f;
    }

    CHECK (measured);

    // ...and switching it off stops answering, rather than handing back the last thing
    // it saw for a graph to go on drawing.
    set.plugin.setOutputAnalysisEnabled (false);
    CHECK_FALSE (set.plugin.copyOutputSpectrumDb (spectrum));
}
