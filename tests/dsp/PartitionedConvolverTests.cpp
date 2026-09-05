#include <dsp/PartitionedConvolver.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

/*
    The convolution engine, which is the one piece of this plugin that is wrong in a
    way nobody can hear until it is very wrong. A filter that is a few samples late,
    or that quietly stops at the end of the first partition, still sounds like a
    cabinet -- so the checks here are the arithmetic ones, against responses whose
    answer can be written down.
*/

namespace
{
    juce::AudioBuffer<float> makeImpulse (int numChannels, int numSamples, int position = 0)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        buffer.clear();

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample (channel, position, 1.0f);

        return buffer;
    }

    /** A response with a value in every tap, so that a convolver which stops early is
        caught by the first tap it drops rather than by luck. */
    juce::AudioBuffer<float> makeRamp (int numSamples)
    {
        juce::AudioBuffer<float> buffer (1, numSamples);

        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, std::sin ((float) i * 0.017f) * 0.5f);

        return buffer;
    }
}

//==============================================================================
TEST_CASE ("The convolver adds no latency of its own", "[dsp][convolver]")
{
    PartitionedConvolver convolver;
    convolver.prepare (1, 4096);

    CHECK (convolver.getLatencySamples() == 0);
}

TEST_CASE ("An impulse through a response reproduces that response", "[dsp][convolver]")
{
    // The definition of the thing, and the check that catches an off-by-one in the
    // head: the first tap has to land on the sample that produced it, not the one
    // after it.
    constexpr int irLength = 1000;

    PartitionedConvolver convolver;
    convolver.prepare (1, irLength);
    convolver.setImpulseResponse (makeRamp (irLength), false);

    const auto expected = makeRamp (irLength);

    // Two blocks, so the tail's own one-frame delay has somewhere to land.
    juce::AudioBuffer<float> buffer = makeImpulse (1, 2048);
    convolver.process (buffer);

    for (int i = 0; i < irLength; ++i)
    {
        INFO ("tap " << i);
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (expected.getSample (0, i), 1.0e-4f));
    }
}

TEST_CASE ("A response shorter than one partition is convolved by the head alone",
           "[dsp][convolver]")
{
    // Cabinet captures are short, and the shortest of them never reach the partitioned
    // half of the engine at all. That path has no frequency-domain delay line to get
    // right, so if it is going to break it breaks completely.
    constexpr int irLength = 64;

    PartitionedConvolver convolver;
    convolver.prepare (1, 8192);
    convolver.setImpulseResponse (makeRamp (irLength), false);

    auto buffer = makeImpulse (1, 512);
    convolver.process (buffer);

    const auto expected = makeRamp (irLength);

    for (int i = 0; i < irLength; ++i)
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (expected.getSample (0, i), 1.0e-4f));
}

TEST_CASE ("A response is convolved to its end however long the engine was prepared for",
           "[dsp][convolver]")
{
    // The engine only multiplies as far as the loaded response reaches, which is what
    // keeps four slots affordable. Getting that bound wrong by one partition truncates
    // the response 256 samples early -- inaudible on a cabinet, and exactly the kind
    // of quiet wrongness worth a test.
    constexpr int irLength = 1500;

    PartitionedConvolver convolver;
    convolver.prepare (1, 96000);
    convolver.setImpulseResponse (makeRamp (irLength), false);

    auto buffer = makeImpulse (1, 4096);
    convolver.process (buffer);

    const auto expected = makeRamp (irLength);

    REQUIRE_THAT (buffer.getSample (0, irLength - 1),
                  Catch::Matchers::WithinAbs (expected.getSample (0, irLength - 1), 1.0e-4f));

    // ...and nothing past it.
    for (int i = irLength; i < irLength + 64; ++i)
        REQUIRE_THAT (buffer.getSample (0, i), Catch::Matchers::WithinAbs (0.0f, 1.0e-4f));
}

TEST_CASE ("Swapping a response mid-stream stays finite and bounded", "[dsp][convolver]")
{
    // Every knob on a strip that reshapes the response hands over a new filter while
    // audio is running. The engine walks to it rather than jumping, and the walk is
    // where a discontinuity would live.
    PartitionedConvolver convolver;
    convolver.prepare (2, 8192);
    convolver.setImpulseResponse (makeRamp (2000), false);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::Random random { 4 };

    auto largest = 0.0f;

    for (int block = 0; block < 64; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

        // Every other block, which is far faster than anything the throttle allows.
        if (block % 2 == 0)
            convolver.setImpulseResponse (makeRamp (500 + block * 20), false);

        convolver.process (buffer);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto sample = buffer.getSample (channel, i);
                REQUIRE (std::isfinite (sample));
                largest = juce::jmax (largest, std::abs (sample));
            }
    }

    // Noise through a response of a few hundred taps has a bound; a filter swap that
    // left stale coefficients in the delay line would sail past it.
    CHECK (largest < 40.0f);
}

TEST_CASE ("A mono response is used for every channel", "[dsp][convolver]")
{
    PartitionedConvolver convolver;
    convolver.prepare (2, 4096);
    convolver.setImpulseResponse (makeRamp (300), false);

    auto buffer = makeImpulse (2, 1024);
    convolver.process (buffer);

    for (int i = 0; i < 300; ++i)
        REQUIRE_THAT (buffer.getSample (1, i),
                      Catch::Matchers::WithinAbs (buffer.getSample (0, i), 1.0e-6f));
}

TEST_CASE ("A signal through the engine matches direct convolution", "[dsp][convolver]")
{
    // The strongest check there is, and the one the impulse tests cannot make: an
    // impulse only ever reads one tap of the frequency-domain path at a time, where a
    // signal reads all of them at once, across frames, with the history ring wrapping
    // underneath. The head arithmetic and the tail's complex multiply-accumulate are
    // both hand-written for speed, and this is what says they still compute a
    // convolution rather than something close to one.
    constexpr int irLength = 900;   // several partitions, and not a multiple of one
    constexpr int signalLength = 2048;

    const auto ir = makeRamp (irLength);

    juce::AudioBuffer<float> signal (1, signalLength);
    juce::Random random { 42 };

    for (int i = 0; i < signalLength; ++i)
        signal.setSample (0, i, random.nextFloat() * 2.0f - 1.0f);

    // Straight from the definition, in double, as the answer to compare against.
    std::vector<double> expected ((size_t) signalLength, 0.0);

    for (int n = 0; n < signalLength; ++n)
        for (int k = 0; k < irLength && k <= n; ++k)
            expected[(size_t) n] += (double) signal.getSample (0, n - k)
                                  * (double) ir.getSample (0, k);

    PartitionedConvolver convolver;
    convolver.prepare (1, 4096);
    convolver.setImpulseResponse (ir, false);

    // In blocks that do not line up with the partition size, so frames are crossed
    // part way through a call.
    juce::AudioBuffer<float> work (1, signalLength);
    work.makeCopyOf (signal);

    for (int start = 0; start < signalLength; start += 100)
    {
        const auto run = juce::jmin (100, signalLength - start);
        juce::AudioBuffer<float> block (work.getArrayOfWritePointers(), 1, start, run);
        convolver.process (block);
    }

    for (int i = 0; i < signalLength; ++i)
    {
        INFO ("sample " << i);
        REQUIRE_THAT (work.getSample (0, i),
                      Catch::Matchers::WithinAbs (expected[(size_t) i], 1.0e-3));
    }
}
