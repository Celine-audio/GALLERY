#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

/*
    Rendering the blend to a file. The one thing an export must never be is a different
    sound from the one being listened to, which is why it runs an impulse through a copy
    of the plugin rather than summing the four responses a second time.
*/

TEST_CASE ("Nothing loaded exports nothing", "[processor][export]")
{
    // A file of silence would be worse than no file: it loads, it convolves, and it
    // makes whatever it is put on disappear.
    PluginProcessor plugin;
    plugin.prepareToPlay (rate, blockSize);

    juce::AudioBuffer<float> rendered;
    CHECK_FALSE (plugin.renderBlend (rendered));
}

TEST_CASE ("The exported response is what the plugin is doing", "[processor][export]")
{
    // The export runs an impulse through a copy of the plugin, so this checks the two
    // agree: convolve the exported file with an impulse and you should get back what
    // the plugin gives an impulse.
    FourCabinets set;
    set.load (4);

    setBlend (set.plugin, -0.4f, 0.3f);
    setParameter (set.plugin, ParamID::pan[1], -0.8f);
    setParameter (set.plugin, ParamID::phase[2], 1.0f);

    juce::AudioBuffer<float> rendered;
    REQUIRE (set.plugin.renderBlend (rendered));

    REQUIRE (rendered.getNumChannels() == 2);
    REQUIRE (rendered.getNumSamples() == (int) std::ceil (Parameters::responseSeconds (0) * rate));

    const auto direct = impulseThrough (set.plugin);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 64; ++i)
        {
            INFO ("channel " << channel << ", sample " << i);
            CHECK_THAT (rendered.getSample (channel, i),
                        Catch::Matchers::WithinAbs (direct.getSample (channel, i), 2.0e-3f));
        }
}

TEST_CASE ("A longer resolution makes the export longer", "[processor][export]")
{
    // The exported response has to be long enough for the longest thing in the blend,
    // or the cabinet somebody turned up is the one the file cuts short. One slot is
    // enough to decide it -- the length is the longest of them, not the commonest.
    FourCabinets set;
    set.load (4);

    juce::AudioBuffer<float> normal;
    REQUIRE (set.plugin.renderBlend (normal));

    const auto top = (int) Parameters::resolutions.size() - 1;
    setParameter (set.plugin, ParamID::resolution[2], (float) top);

    juce::AudioBuffer<float> longer;
    REQUIRE (set.plugin.renderBlend (longer));

    INFO (normal.getNumSamples() << " -> " << longer.getNumSamples());

    CHECK (normal.getNumSamples() == (int) std::ceil (Parameters::responseSeconds (0) * rate));
    CHECK (longer.getNumSamples() == (int) std::ceil (Parameters::responseSeconds (top) * rate));
}

TEST_CASE ("The output trim stays out of the export", "[processor][export]")
{
    // How loud the plugin sits in a mix is not a property of the cabinet. Baked into
    // the file it would make an export that stopped matching the blend the moment the
    // trim was touched.
    FourCabinets set;
    set.load (2);

    juce::AudioBuffer<float> flat;
    REQUIRE (set.plugin.renderBlend (flat));

    setParameter (set.plugin, ParamID::outputGain, 12.0f);

    juce::AudioBuffer<float> trimmed;
    REQUIRE (set.plugin.renderBlend (trimmed));

    const auto before = flat.getMagnitude (0, flat.getNumSamples());
    const auto after = trimmed.getMagnitude (0, trimmed.getNumSamples());

    INFO ("peak " << before << " against " << after);
    CHECK_THAT (after, Catch::Matchers::WithinRel (before, 0.01f));
}
