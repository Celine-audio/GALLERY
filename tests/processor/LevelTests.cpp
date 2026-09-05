#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

/*
    **The plugin comes out at the same level whatever is loaded in it.** Not with no
    cabinet against one, not with one against four, not before a mute against after. A
    cab loader that gets louder every time you give it something to do is one that has
    to be re-gain-staged after every decision, and it was exactly that until the
    responses were levelled and the sum divided by what is in it.
*/

//==============================================================================
TEST_CASE ("A cabinet does not change how loud the track is", "[processor][level]")
{
    // The complaint this answers, in its simplest form: insert the plugin, load one
    // cabinet, and the level should be where it was.
    FourCabinets set;

    juce::AudioBuffer<float> dry (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 21 };

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < blockSize; ++i)
            dry.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.makeCopyOf (dry);
    set.plugin.processBlock (buffer, midi);

    const auto before = buffer.getRMSLevel (0, 0, blockSize);

    set.load (1);

    // Several blocks, so the engine's crossfade into the new filter has finished.
    for (int block = 0; block < 24; ++block)
    {
        buffer.makeCopyOf (dry);
        set.plugin.processBlock (buffer, midi);
    }

    const auto after = buffer.getRMSLevel (0, 0, blockSize);

    INFO ("dry " << before << ", with a cabinet " << after);
    CHECK_THAT (after, Catch::Matchers::WithinRel (before, 0.15f));
}

TEST_CASE ("The level is the same for one cabinet as for four", "[processor][level]")
{
    // The second half of the complaint: it got louder again with every cabinet added.
    // All four spikes land at the same place here, so they stack -- which is the worst
    // case, and the one that used to be four times as loud.
    juce::TemporaryFile temporary;
    const auto directory = temporary.getFile().getParentDirectory();

    std::array<juce::File, 4> files;

    for (int slot = 0; slot < 4; ++slot)
        files[(size_t) slot] = writeSpike (directory, "gallery-stack-" + juce::String (slot),
                                           0.1f * (float) (slot + 1), 0);

    PluginProcessor plugin;
    plugin.prepareToPlay (rate, blockSize);

    REQUIRE (plugin.loadImpulseResponse (0, files[0]).wasOk());
    const auto one = peakOf (plugin);

    for (int slot = 1; slot < 4; ++slot)
        REQUIRE (plugin.loadImpulseResponse (slot, files[(size_t) slot]).wasOk());

    const auto four = peakOf (plugin);

    INFO ("one cabinet " << one << ", four " << four);
    CHECK_THAT (four, Catch::Matchers::WithinRel (one, 0.05f));

    // ...and both at unity, not merely at each other.
    CHECK_THAT (one, Catch::Matchers::WithinAbs (1.0f, 0.05f));

    for (const auto& file : files)
        file.deleteFile();
}

TEST_CASE ("A cabinet at the centre is not quietly turned down", "[processor][level]")
{
    // Equal-power panning referred to the corners puts 0.707 a side at the centre,
    // which would leave every slot three decibels down at the setting all four of them
    // start at -- a plugin quieter than no plugin, for a reason nothing on screen
    // explains.
    FourCabinets set;
    set.load (1);

    const auto output = impulseThrough (set.plugin);

    CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (1.0f, 0.05f));
    CHECK_THAT (output.getSample (1, 0), Catch::Matchers::WithinAbs (1.0f, 0.05f));
}
