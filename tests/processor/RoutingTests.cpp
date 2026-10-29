#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <functional>

using namespace Cabinets;

/*
    How the cabinets are wired. The fixture's cabinets are single spikes ten samples
    apart, which levelling turns into pure delays at unity -- so a cabinet fed into
    another is the two delays added together, and where a spike lands in the output says
    exactly which cabinets it went through.
*/

namespace
{
    void setRouting (PluginProcessor& plugin, Parameters::Routing routing)
    {
        setParameter (plugin, ParamID::routing, (float) routing);
    }

    /** Where cabinet `slot`'s spike sits, which is how far it delays what goes in. */
    constexpr int delayOf (int slot) { return slot * 10; }

    float at (const juce::AudioBuffer<float>& buffer, int index)
    {
        return buffer.getSample (0, index);
    }
}

//==============================================================================
TEST_CASE ("Parallel is what GALLERY always did", "[processor][routing]")
{
    // Every session saved before routing existed opens on the default, so the default
    // has to be the old sound exactly.
    Parameters::BlendWeights expected {};

    for (const auto& [x, y] : { std::pair { 0.0f, 0.0f }, std::pair { -0.7f, 0.4f },
                                std::pair { 1.0f, -1.0f } })
    {
        expected = Parameters::blendWeights (x, y);

        const auto mix = Parameters::mixFor (Parameters::Routing::parallel, x, y,
                                             { true, true, true, true },
                                             {}, {});

        for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
        {
            INFO ("pad at " << x << ", " << y << ", slot " << slot);
            CHECK (mix.engaged[slot]);
            CHECK_THAT (mix.share[slot], Catch::Matchers::WithinAbs (expected[slot], 1.0e-6f));
        }
    }
}

TEST_CASE ("The chains' shares sum to one under every routing", "[processor][routing]")
{
    for (const auto routing : { Parameters::Routing::parallel, Parameters::Routing::twoChains,
                                Parameters::Routing::series })
        for (const auto x : { -1.0f, -0.3f, 0.0f, 0.8f })
            for (const auto y : { -1.0f, 0.2f, 1.0f })
            {
                const auto mix = Parameters::mixFor (routing, x, y, { true, true, true, true }, {}, {});

                auto total = 0.0f;

                for (const auto share : mix.share)
                    total += share;

                INFO ("routing " << (int) routing << ", pad at " << x << ", " << y);
                CHECK_THAT (total, Catch::Matchers::WithinAbs (1.0f, 1.0e-5f));
            }
}

TEST_CASE ("Series runs the four cabinets one after another", "[processor][routing]")
{
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::series);

    const auto output = impulseThrough (set.plugin);
    const auto end = delayOf (0) + delayOf (1) + delayOf (2) + delayOf (3);

    // One spike, through all four, at full level -- and nothing where any one of them
    // on its own would have put it.
    CHECK_THAT (at (output, end), Catch::Matchers::WithinAbs (1.0f, 0.03f));

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        INFO ("cabinet " << slot << " alone");
        CHECK_THAT (at (output, delayOf (slot)), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    }
}

TEST_CASE ("Two chains run 1 into 2 beside 3 into 4", "[processor][routing]")
{
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::twoChains);

    const auto output = impulseThrough (set.plugin);

    // The pad centred: the two chains half each.
    CHECK_THAT (at (output, delayOf (0) + delayOf (1)), Catch::Matchers::WithinAbs (0.5f, 0.03f));
    CHECK_THAT (at (output, delayOf (2) + delayOf (3)), Catch::Matchers::WithinAbs (0.5f, 0.03f));

    // And nothing where cabinets 3 and 4 on their own would be, which is where a
    // parallel sum would put them. (1 and 2 on their own land at 0 and 10, and the first
    // cabinet is no delay at all, so the top chain is at 10 either way.)
    CHECK_THAT (at (output, delayOf (2)), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    CHECK_THAT (at (output, delayOf (3)), Catch::Matchers::WithinAbs (0.0f, 0.03f));
}

TEST_CASE ("With two chains the pad's height blends them and its width does not",
           "[processor][routing][blend]")
{
    // The chains are the pad's two rows, so the height is the only direction left that
    // means anything -- and the only one the dot can move in. A width that still did
    // something would be a control the pad no longer lets anybody reach.
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::twoChains);

    for (const auto x : { -1.0f, 0.0f, 1.0f })
    {
        setBlend (set.plugin, x, 1.0f);

        const auto output = impulseThrough (set.plugin);
        const auto top = at (output, delayOf (0) + delayOf (1));
        const auto bottom = at (output, delayOf (2) + delayOf (3));

        INFO ("pad at " << x << ", 1: top " << top << ", bottom " << bottom);

        // A row pushed to the top is that row at 0.9 -- the floor keeps a twentieth of
        // each cabinet in the other.
        CHECK_THAT (top, Catch::Matchers::WithinAbs (0.9f, 0.03f));
        CHECK_THAT (bottom, Catch::Matchers::WithinAbs (0.1f, 0.03f));
    }
}

TEST_CASE ("In series the pad changes nothing", "[processor][routing][blend]")
{
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::series);

    const auto end = delayOf (0) + delayOf (1) + delayOf (2) + delayOf (3);

    for (const auto& [x, y] : { std::pair { 0.0f, 0.0f }, std::pair { -1.0f, 1.0f }, std::pair { 0.6f, -0.8f } })
    {
        setBlend (set.plugin, x, y);
        INFO ("pad at " << x << ", " << y);
        CHECK_THAT (at (impulseThrough (set.plugin), end), Catch::Matchers::WithinAbs (1.0f, 0.03f));
    }
}

TEST_CASE ("A muted cabinet in a chain lets the signal past", "[processor][routing]")
{
    // Silence would be the wrong answer: muting the second of four would silence all
    // four, which is not what anybody muting one cabinet is asking for.
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::series);
    setParameter (set.plugin, ParamID::mute[1], 1.0f);

    const auto output = impulseThrough (set.plugin);
    const auto without = delayOf (0) + delayOf (2) + delayOf (3);

    CHECK_THAT (at (output, without), Catch::Matchers::WithinAbs (1.0f, 0.03f));
    CHECK_THAT (at (output, without + delayOf (1)), Catch::Matchers::WithinAbs (0.0f, 0.03f));
}

TEST_CASE ("Solo in a chain is that cabinet on its own", "[processor][routing]")
{
    // Solo means "let me hear this one", and in a chain that is the cabinet alone rather
    // than everything it happens to be wired to.
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::series);
    setParameter (set.plugin, ParamID::solo[2], 1.0f);

    const auto output = impulseThrough (set.plugin);

    CHECK_THAT (at (output, delayOf (2)), Catch::Matchers::WithinAbs (1.0f, 0.03f));
    CHECK_THAT (at (output, delayOf (0) + delayOf (1) + delayOf (2) + delayOf (3)),
                Catch::Matchers::WithinAbs (0.0f, 0.03f));
}

TEST_CASE ("An empty slot in a chain is skipped rather than silencing it", "[processor][routing]")
{
    FourCabinets set;
    set.load (3);
    setRouting (set.plugin, Parameters::Routing::twoChains);

    const auto output = impulseThrough (set.plugin);

    // The bottom chain is cabinet 3 alone, and still gets its half.
    CHECK_THAT (at (output, delayOf (0) + delayOf (1)), Catch::Matchers::WithinAbs (0.5f, 0.03f));
    CHECK_THAT (at (output, delayOf (2)), Catch::Matchers::WithinAbs (0.5f, 0.03f));
}

TEST_CASE ("Muting a whole chain hands the output to the other", "[processor][routing]")
{
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::twoChains);
    setParameter (set.plugin, ParamID::mute[0], 1.0f);
    setParameter (set.plugin, ParamID::mute[1], 1.0f);

    const auto output = impulseThrough (set.plugin);

    // Silence for the muted chain, not its input passed through two see-through
    // cabinets: a chain with nothing audible in it is a chain nobody is listening to.
    CHECK_THAT (at (output, 0), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    CHECK_THAT (at (output, delayOf (2) + delayOf (3)), Catch::Matchers::WithinAbs (1.0f, 0.03f));
}

TEST_CASE ("Changing the routing does not click", "[processor][routing][clicks]")
{
    // Swapped in one sample, a cabinet's input goes from the dry signal to another
    // cabinet's output -- a step in the waveform as big as the signal. The change is
    // taken with the cabinets faded out instead.
    constexpr int toneBlock = 64;

    for (const auto [from, to] : { std::pair { Parameters::Routing::parallel, Parameters::Routing::series },
                                   std::pair { Parameters::Routing::series, Parameters::Routing::twoChains },
                                   std::pair { Parameters::Routing::twoChains, Parameters::Routing::parallel } })
    {
        FourCabinets set;
        set.load (4);
        setRouting (set.plugin, from);

        juce::AudioBuffer<float> buffer (2, toneBlock);
        juce::MidiBuffer midi;
        std::vector<float> output;
        double phase = 0.0;

        const auto run = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < toneBlock; ++i)
                {
                    const auto sample = (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 220.0 / rate;
                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, sample);
                }

                set.plugin.processBlock (buffer, midi);

                for (int i = 0; i < toneBlock; ++i)
                    output.push_back (buffer.getSample (0, i));
            }
        };

        const auto curvature = [&output] (int begin, int end)
        {
            auto worst = 0.0f;

            for (auto i = juce::jmax (2, begin); i < end; ++i)
                worst = juce::jmax (worst, std::abs (output[(size_t) i] - 2.0f * output[(size_t) i - 1]
                                                     + output[(size_t) i - 2]));

            return worst;
        };

        run (60);
        const auto changedAt = (int) output.size();

        setRouting (set.plugin, to);
        run (60);

        const auto before = curvature (changedAt / 2, changedAt);
        const auto after = curvature (changedAt, (int) output.size());

        INFO ("from " << (int) from << " to " << (int) to << ": baseline " << before << ", after " << after);
        CHECK (after < before * 3.0f);
    }
}

TEST_CASE ("A chain exports at its whole length", "[processor][routing][export]")
{
    // A chain rings for its responses end to end -- each cabinet convolves the whole of
    // the tail before it -- so an export long enough for the longest single response
    // would cut a chain short.
    FourCabinets set;
    set.load (4);
    setRouting (set.plugin, Parameters::Routing::series);

    juce::AudioBuffer<float> rendered;
    REQUIRE (set.plugin.renderBlend (rendered));

    CHECK (rendered.getNumSamples()
           == (int) std::ceil (Parameters::responseSeconds (0) * ParamID::numSlots * rate));

    // And is what the plugin gives an impulse.
    const auto direct = impulseThrough (set.plugin);
    const auto end = delayOf (0) + delayOf (1) + delayOf (2) + delayOf (3);

    CHECK_THAT (rendered.getSample (0, end), Catch::Matchers::WithinAbs (direct.getSample (0, end), 2.0e-3f));
}
