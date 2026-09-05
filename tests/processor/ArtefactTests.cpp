#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

#include <functional>

/*
    Things you can hear that are not the plugin doing its job: a step in the waveform
    where a control moved, a filter switching on from silence, a response arriving over
    input history that has gone stale. Every one of these was measured as a fault before
    it was fixed, and the numbers in the comments are what it measured.
*/

//==============================================================================
namespace
{
    /** The worst second difference in a signal: near zero for anything smooth, whatever
        its shape, and a spike wherever the waveform actually breaks.

        The second difference rather than the first, because a crossfade between two
        different signals moves the first derivative quite legitimately -- measuring
        that flags every honest transition as a click and misses nothing else. */
    float worstCurvature (const std::vector<float>& signal, int from, int to)
    {
        auto worst = 0.0f;

        for (int i = juce::jmax (2, from); i < juce::jmin (to, (int) signal.size()); ++i)
            worst = juce::jmax (worst, std::abs (signal[(size_t) i]
                                                 - 2.0f * signal[(size_t) i - 1]
                                                 + signal[(size_t) i - 2]));

        return worst;
    }

    /** Runs a steady tone through the plugin, calling `disturb` half way, and returns
        every output sample so the join can be inspected. */
    struct ToneRun
    {
        std::vector<float> output;
        int disturbedAt = 0;

        float baseline() const { return worstCurvature (output, 2, disturbedAt); }
        float afterwards() const { return worstCurvature (output, disturbedAt, (int) output.size()); }
    };

    ToneRun runTone (PluginProcessor& plugin, const std::function<void()>& disturb,
                     int blocksEitherSide = 60)
    {
        constexpr int toneBlock = 64;

        juce::AudioBuffer<float> buffer (2, toneBlock);
        juce::MidiBuffer midi;

        ToneRun result;
        double phase = 0.0;
        const auto advance = juce::MathConstants<double>::twoPi * 220.0 / rate;

        const auto run = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < toneBlock; ++i)
                {
                    const auto sample = (float) std::sin (phase);
                    phase += advance;
                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, sample);
                }

                plugin.processBlock (buffer, midi);

                for (int i = 0; i < toneBlock; ++i)
                    result.output.push_back (buffer.getSample (0, i));
            }
        };

        run (blocksEitherSide);
        result.disturbedAt = (int) result.output.size();

        disturb();

        run (blocksEitherSide);

        return result;
    }
}

TEST_CASE ("Emptying a slot fades rather than cuts", "[processor][clicks]")
{
    // Measured before this was fixed: the worst break in the waveform went up
    // sixty-sevenfold at the moment of the unload, at exactly one partition after it.
    // The engine walks between filters in eight steps taken at frame boundaries, and
    // stepping from a cabinet to silence is a twelfth of the signal disappearing at a
    // stroke, four times over before anything else could fade it.
    FourCabinets set;
    set.load (1);

    const auto tone = runTone (set.plugin, [&] { set.plugin.unloadImpulseResponse (0); });

    INFO ("baseline " << tone.baseline() << ", after unload " << tone.afterwards());
    CHECK (tone.afterwards() < tone.baseline() * 3.0f);
}

TEST_CASE ("Dragging Align does not step the filter", "[processor][clicks]")
{
    // The same fault, and the one that was actually reported: every rebuild hands the
    // engine a new filter, and an alignment drag hands it a long series of them. It
    // measured a hundred and ninety times the baseline before the head was crossfaded
    // sample by sample rather than stepped frame by frame.
    FourCabinets set;
    set.load (1);

    auto* align = set.plugin.getAPVTS().getParameter (ParamID::align[0]);
    REQUIRE (align != nullptr);

    const auto tone = runTone (set.plugin, [&]
    {
        for (int step = 1; step <= 6; ++step)
        {
            align->setValueNotifyingHost (align->convertTo0to1 ((float) step * 0.8f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
    });

    INFO ("baseline " << tone.baseline() << ", during the drag " << tone.afterwards());
    CHECK (tone.afterwards() < tone.baseline() * 3.0f);
}

TEST_CASE ("A slot filled again does not play back what it last heard", "[processor][clicks]")
{
    // An emptied slot stops calling its convolution engine once its fade has landed, so
    // the engine's input history freezes at whatever was playing then. Loading a cabinet
    // back into it convolved that history: measured at -25 dBFS of the past, arriving on
    // a signal that is now silent -- and clearing a slot to try a different cabinet is
    // an ordinary thing to do with the track running.
    FourCabinets set;

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 11 };

    const auto noise = [&] (int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

            set.plugin.processBlock (buffer, midi);
        }
    };

    const auto loudestInSilence = [&] (int blocks)
    {
        auto worst = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();
            set.plugin.processBlock (buffer, midi);
            worst = juce::jmax (worst, buffer.getMagnitude (0, blockSize));
        }

        return worst;
    };

    set.load (1);
    noise (40);

    set.plugin.unloadImpulseResponse (0);

    // Long enough for the slot's own fade to land, which is when it stops running its
    // engine at all.
    noise (20);

    REQUIRE (set.plugin.loadImpulseResponse (0, set.files[0]).wasOk());

    const auto ghost = loudestInSilence (20);

    INFO ("loudest sample in silence after reloading: " << ghost);
    CHECK (ghost == 0.0f);
}

//==============================================================================
// Dragging a control must not be audible as anything but the control moving. Both of
// these were measured as faults before they were fixed, and the numbers in the comments
// are what they were.

namespace
{
    /** Sweeps one parameter the way a fast drag does and reports how much worse the
        worst break in the waveform gets while it moves.

        Blocks of 512 with the message loop given real time between them, so anything
        the message thread throttles sees the clock a host would give it. */
    float sweepRatio (const char* id, float from, float to, int blocks)
    {
        FourCabinets set;
        set.load (1);

        const auto move = [&set, id] (float value)
        {
            auto* p = set.plugin.getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };

        move (from);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<float> out;
        double phase = 0.0;
        const auto advance = juce::MathConstants<double>::twoPi * 220.0 / rate;

        const auto run = [&] (int count, const std::function<void (int)>& perBlock)
        {
            for (int b = 0; b < count; ++b)
            {
                if (perBlock != nullptr)
                    perBlock (b);

                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, (float) std::sin (phase));
                    buffer.setSample (1, i, (float) std::sin (phase));
                    phase += advance;
                }

                set.plugin.processBlock (buffer, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (11);

                for (int i = 0; i < 512; ++i)
                    out.push_back (buffer.getSample (0, i));
            }
        };

        run (40, nullptr);
        const auto settled = (int) out.size();

        run (blocks, [&] (int b) { move (from + (to - from) * (float) b / (float) blocks); });

        // From the second half of the settle, so the transient of the first filter
        // arriving over silence is not taken for the resting state.
        const auto baseline = worstCurvature (out, settled / 2, settled);
        const auto sweeping = worstCurvature (out, settled, (int) out.size());

        return sweeping / juce::jmax (1.0e-9f, baseline);
    }
}

TEST_CASE ("Sweeping a cut does not crackle", "[processor][clicks]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    // A cut spends most of its life parked at the end of its travel, switched off. Moved
    // off that end it used to switch on from silence, so its first output was its *step*
    // response rather than its steady one -- a jump of a third of full scale into
    // whatever was playing. Measured on the high cut at 24 dB/octave, the worst break in
    // the waveform went up eleven hundred fold. It is faded in now.
    //
    // The rest of the sweep is coefficients being rebuilt: a cascade redesigned between
    // one block and the next steps, and a step in an IIR holding energy is a transient.
    // The frequency is ramped and the coefficients rebuilt several times a block.
    INFO ("low cut 20 -> 2000 Hz");
    CHECK (sweepRatio (ParamID::lowCut[0], 20.0f, 2000.0f, 30) < 12.0f);

    INFO ("high cut 20 kHz -> 300 Hz");
    CHECK (sweepRatio (ParamID::highCut[0], 20000.0f, 300.0f, 30) < 30.0f);
}

TEST_CASE ("Sweeping alignment does not crackle", "[processor][clicks]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Alignment is a delay line on the audio thread, moved a sample at a time. It used
    // to be a shift baked into the response, which meant a rebuild per move -- and the
    // engine walks between two filters over about forty milliseconds, so rebuilds any
    // faster than that arrived mid-walk and cut it short. Measured at a 45 ms throttle,
    // the worst break went up a hundred and forty fold; at 150 ms it was clean but the
    // control moved in six steps a second, which is what "not smooth" was.
    INFO ("align 0 -> 6 ms");
    CHECK (sweepRatio (ParamID::align[0], 0.0f, 6.0f, 45) < 4.0f);
}
