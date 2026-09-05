#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

/*
    The blend pad, and the two switches that override it.
*/

//==============================================================================
// The blend pad. The four test cabinets are single spikes ten samples apart, so each
// one's share of the output is simply the height of its own spike -- which is what
// makes the blend law readable straight off the buffer.

namespace
{
    /** The four spike heights, in slot order. */
    std::array<float, 4> sharesOf (PluginProcessor& plugin)
    {
        const auto output = impulseThrough (plugin);

        return { output.getSample (0, 0),  output.getSample (0, 10),
                 output.getSample (0, 20), output.getSample (0, 30) };
    }
}

TEST_CASE ("The blend weights sum to one wherever the dot is", "[processor][blend]")
{
    // The level rule, as arithmetic. Four microphones on one speaker are largely
    // correlated, so they add nearly arithmetically -- which means the shares have to
    // sum to one or the plugin changes loudness as the dot is dragged about.
    for (const auto x : { -1.0f, -0.4f, 0.0f, 0.3f, 1.0f })
        for (const auto y : { -1.0f, -0.7f, 0.0f, 0.6f, 1.0f })
        {
            const auto weights = Parameters::blendWeights (x, y);
            const auto total = weights[0] + weights[1] + weights[2] + weights[3];

            INFO ("at (" << x << ", " << y << ")");
            CHECK_THAT (total, Catch::Matchers::WithinAbs (1.0f, 1.0e-5f));

            for (const auto weight : weights)
                CHECK (weight >= 0.0f);
        }
}

TEST_CASE ("The centre of the pad is all four cabinets equally", "[processor][blend]")
{
    const auto weights = Parameters::blendWeights (0.0f, 0.0f);

    for (const auto weight : weights)
        CHECK_THAT (weight, Catch::Matchers::WithinAbs (0.25f, 1.0e-5f));
}

TEST_CASE ("Each corner of the pad favours its own cabinet", "[processor][blend]")
{
    // Slot 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right, with y positive
    // upwards. Written out rather than looped, because the whole point of this test is
    // that the four corners are *not* interchangeable and a loop over an index would
    // pass just as happily with two of them swapped.
    struct Corner { float x, y; int slot; };

    constexpr auto even = Parameters::blendFloor / (float) ParamID::numSlots;
    constexpr auto most = even + (1.0f - Parameters::blendFloor);

    for (const auto corner : { Corner { -1.0f,  1.0f, 0 }, Corner {  1.0f,  1.0f, 1 },
                               Corner { -1.0f, -1.0f, 2 }, Corner {  1.0f, -1.0f, 3 } })
    {
        const auto weights = Parameters::blendWeights (corner.x, corner.y);

        INFO ("corner (" << corner.x << ", " << corner.y << ") should favour slot " << corner.slot);

        for (int slot = 0; slot < ParamID::numSlots; ++slot)
            CHECK_THAT (weights[(size_t) slot],
                        Catch::Matchers::WithinAbs (slot == corner.slot ? most : even, 1.0e-5f));
    }
}

TEST_CASE ("No cabinet is ever silenced by the pad alone", "[processor][blend]")
{
    // Taking a cabinet out of the blend altogether is what the mute button is for. A
    // pad that also did it would be two controls doing one job, with no way to tell
    // from the pad which of them had been used.
    for (const auto x : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        for (const auto y : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            for (const auto weight : Parameters::blendWeights (x, y))
            {
                INFO ("at (" << x << ", " << y << ")");
                CHECK (weight > 0.01f);
            }
}

TEST_CASE ("The pad moves the balance between cabinets", "[processor][blend]")
{
    FourCabinets set;
    set.load (4);

    setBlend (set.plugin, 0.0f, 0.0f);
    const auto centred = sharesOf (set.plugin);

    // Four spikes that do not overlap, so the sum of their heights is the sum of the
    // weights -- which the law above says is one, whatever the dot is doing.
    const auto totalAt = [] (const std::array<float, 4>& shares)
    {
        return shares[0] + shares[1] + shares[2] + shares[3];
    };

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        INFO ("slot " << slot << " against slot 0 at the centre");
        CHECK_THAT (centred[(size_t) slot], Catch::Matchers::WithinRel (centred[0], 0.05f));
    }

    setBlend (set.plugin, 1.0f, 1.0f);   // slot 1's corner
    const auto cornered = sharesOf (set.plugin);

    CHECK (cornered[1] > centred[1] * 3.0f);

    for (const auto slot : { 0, 2, 3 })
    {
        INFO ("slot " << slot << " should be well down, but still there");
        CHECK (cornered[(size_t) slot] < centred[(size_t) slot] * 0.5f);
        CHECK (cornered[(size_t) slot] > 0.0f);
    }

    INFO ("centred total " << totalAt (centred) << ", cornered " << totalAt (cornered));
    CHECK_THAT (totalAt (cornered), Catch::Matchers::WithinRel (totalAt (centred), 0.05f));
}

TEST_CASE ("Muting shares the output between what is left", "[processor][blend]")
{
    // The rule the old per-slot trim followed, and it still has to hold now the shares
    // are unequal: muting one of four turns the other three up rather than quietly
    // dropping the output by a quarter.
    FourCabinets set;
    set.load (4);

    setBlend (set.plugin, 0.0f, 0.0f);
    const auto before = sharesOf (set.plugin);

    setParameter (set.plugin, ParamID::mute[3], 1.0f);
    const auto after = sharesOf (set.plugin);

    CHECK_THAT (after[3], Catch::Matchers::WithinAbs (0.0f, 0.02f));

    // A quarter each became a third each.
    for (const auto slot : { 0, 1, 2 })
    {
        INFO ("slot " << slot << ": " << before[(size_t) slot] << " -> " << after[(size_t) slot]);
        CHECK_THAT (after[(size_t) slot] / before[(size_t) slot],
                    Catch::Matchers::WithinRel (4.0f / 3.0f, 0.05f));
    }
}

TEST_CASE ("Solo hands the whole output to what is soloed", "[processor][blend]")
{
    // However far the pad is from it. Put the dot on slot 1's corner, where slot 3 has
    // only its floor, then solo slot 3: solo is an instruction to listen to something,
    // so it has to arrive at full strength rather than at the share the pad left it.
    FourCabinets set;
    set.load (4);

    setBlend (set.plugin, 1.0f, 1.0f);
    setParameter (set.plugin, ParamID::solo[3], 1.0f);

    const auto shares = sharesOf (set.plugin);

    INFO ("slot 3 at " << shares[3]);
    CHECK (std::abs (shares[3]) > 0.5f);

    for (const auto slot : { 0, 1, 2 })
        CHECK_THAT (shares[(size_t) slot], Catch::Matchers::WithinAbs (0.0f, 0.02f));
}

TEST_CASE ("Muting every cabinet is silence", "[processor][blend]")
{
    // Four muted cabinets are still four cabinets, and the answer is silence rather
    // than the signal that went in -- turning anything up would be substituting a
    // sound nobody asked for.
    FourCabinets set;
    set.load (4);

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
        setParameter (set.plugin, ParamID::mute[(size_t) slot], 1.0f);

    CHECK_THAT (peakOf (set.plugin), Catch::Matchers::WithinAbs (0.0f, 0.02f));
}

TEST_CASE ("Solo and mute decide who is in the sum, and share it out again",
           "[processor]")
{
    FourCabinets set;
    set.load (4);

    SECTION ("one soloed leaves only that one, at full level")
    {
        setParameter (set.plugin, ParamID::solo[2], 1.0f);

        const auto output = impulseThrough (set.plugin);

        // Alone in the output, so it gets all of it rather than the quarter it had.
        CHECK_THAT (output.getSample (0, 20), Catch::Matchers::WithinAbs (1.0f, 0.05f));
        CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (0.0f, 0.03f));
        CHECK_THAT (output.getSample (0, 30), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    }

    SECTION ("muting one shares the output between the other three")
    {
        setParameter (set.plugin, ParamID::mute[3], 1.0f);

        const auto output = impulseThrough (set.plugin);

        // A third each, not a quarter: a muted cabinet taking its share of the output
        // with it would make muting one of four quieten the other three.
        for (const auto position : { 0, 10, 20 })
            CHECK_THAT (output.getSample (0, position),
                        Catch::Matchers::WithinAbs (1.0f / 3.0f, 0.03f));

        CHECK_THAT (output.getSample (0, 30), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    }

    SECTION ("two soloed share it between them")
    {
        setParameter (set.plugin, ParamID::solo[0], 1.0f);
        setParameter (set.plugin, ParamID::solo[3], 1.0f);

        const auto output = impulseThrough (set.plugin);

        CHECK_THAT (output.getSample (0, 0), Catch::Matchers::WithinAbs (0.5f, 0.03f));
        CHECK_THAT (output.getSample (0, 30), Catch::Matchers::WithinAbs (0.5f, 0.03f));
        CHECK_THAT (output.getSample (0, 10), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    }

    SECTION ("mute wins over that slot's own solo")
    {
        // Both set on one slot is not a state anybody chooses on purpose, but a host
        // automating both can produce it, and "muted but soloed" has to mean something
        // rather than whichever branch happens to be tested first.
        setParameter (set.plugin, ParamID::solo[1], 1.0f);
        setParameter (set.plugin, ParamID::mute[1], 1.0f);

        CHECK_THAT (peakOf (set.plugin), Catch::Matchers::WithinAbs (0.0f, 0.03f));
    }
}
