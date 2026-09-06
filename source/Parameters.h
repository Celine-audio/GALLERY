#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/**
    The plugin's parameters: their IDs in one place, and the layout that declares them.
    Everything that reads a parameter goes through ParamID rather than a string
    literal, so a typo is a compile error instead of a null pointer at runtime.

    Four cabinets of eleven controls is forty-four parameters, which is well past the
    point where a hand-kept list stays in step with anything. So the per-slot IDs are
    arrays indexed by slot, and every one of them is written out rather than built by
    concatenation: an ID that is assembled at runtime cannot be checked by the
    compiler, and a slot loop that quietly asks for "ir5Gain" gets a null parameter
    and a crash in somebody's host rather than a build failure here.
*/
namespace ParamID
{
    inline constexpr auto bypass     = "bypass";
    inline constexpr auto outputGain = "outputGain";

    /** Where the blend pad's dot sits, as -1 to 1 on each axis with the centre at
        zero. Two parameters rather than four levels, because the pad *is* the control:
        a host automating it moves a point around a square, which is the gesture, where
        four gains that have to stay summing to one is the gesture taken apart and
        handed over in pieces that can each be set to something impossible. */
    inline constexpr auto blendX = "blendX";
    inline constexpr auto blendY = "blendY";

    /** How many cabinets. Four is the design, and the whole window is laid out around
        it, so this is documentation rather than a thing to turn up. */
    inline constexpr int numSlots = 4;

    template <typename T>
    using PerSlot = std::array<T, (size_t) numSlots>;

    inline constexpr PerSlot<const char*> solo  { "ir1Solo",  "ir2Solo",  "ir3Solo",  "ir4Solo"  };
    inline constexpr PerSlot<const char*> mute  { "ir1Mute",  "ir2Mute",  "ir3Mute",  "ir4Mute"  };
    inline constexpr PerSlot<const char*> phase { "ir1Phase", "ir2Phase", "ir3Phase", "ir4Phase" };

    inline constexpr PerSlot<const char*> align  { "ir1Align",  "ir2Align",  "ir3Align",  "ir4Align"  };
    inline constexpr PerSlot<const char*> pan    { "ir1Pan",    "ir2Pan",    "ir3Pan",    "ir4Pan"    };

    inline constexpr PerSlot<const char*> resolution { "ir1Res", "ir2Res", "ir3Res", "ir4Res" };

    inline constexpr PerSlot<const char*> lowCut  { "ir1LowCut",  "ir2LowCut",  "ir3LowCut",  "ir4LowCut"  };
    inline constexpr PerSlot<const char*> highCut { "ir1HighCut", "ir2HighCut", "ir3HighCut", "ir4HighCut" };

    inline constexpr PerSlot<const char*> lowCutSlope  { "ir1LowCutSlope",  "ir2LowCutSlope",
                                                         "ir3LowCutSlope",  "ir4LowCutSlope"  };
    inline constexpr PerSlot<const char*> highCutSlope { "ir1HighCutSlope", "ir2HighCutSlope",
                                                         "ir3HighCutSlope", "ir4HighCutSlope" };

    /** The controls a move of which means rebuilding a response rather than reading a
        number -- which is only how much of it to keep. Everything else on a strip,
        alignment included, is read per block and costs nothing.

        An alias rather than a second list of the same four strings: the processor's
        listener and the rebuild it throttles have to agree about this, and two lists
        agree only until one of them is edited. */
    inline constexpr auto& shaping = resolution;

    /** Where a slot's file is remembered. Not a parameter -- a host cannot automate a
        path and would not know what to do with one -- so it rides in the state tree
        beside the window size. */
    inline constexpr PerSlot<const char*> fileProperty {
        "ir1File", "ir2File", "ir3File", "ir4File"
    };

    /** Which side of a stereo file that slot was loaded from -- "left", "right", or
        absent for the whole file. Beside the path because it is part of which response
        this is: a session that remembered the file and forgot the side would reopen
        sounding different. */
    inline constexpr PerSlot<const char*> sideProperty {
        "ir1Side", "ir2Side", "ir3Side", "ir4Side"
    };

    /** And where the last one was chosen from, for the same reason: a folder is not a
        parameter, but forgetting it between four slots is an annoyance. */
    inline constexpr auto browseDirectoryProperty = "browseDirectory";

    /** The folder the library is listing. Kept for the same reason and in the same
        place: it is not a parameter, and reopening a session to an empty browser is
        the folder having to be found again every time. */
    inline constexpr auto libraryFolderProperty = "libraryFolder";
}

namespace Parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** One slot's parameter values, looked up once and held by address.

        `getRawParameterValue` searches by string. The processor reads all eleven of
        these for four slots on every audio block, and the editor reads most of them
        thirty times a second; neither wants fifty string lookups to fetch numbers whose
        addresses never change. */
    struct SlotPointers
    {
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* phase = nullptr;
        std::atomic<float>* resolution = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* lowCut = nullptr;
        std::atomic<float>* highCut = nullptr;
        std::atomic<float>* lowCutSlope = nullptr;
        std::atomic<float>* highCutSlope = nullptr;
        std::atomic<float>* align = nullptr;

        static bool isOn (const std::atomic<float>* flag) noexcept
        {
            return flag != nullptr && flag->load() > 0.5f;
        }
    };

    using AllSlotPointers = ParamID::PerSlot<SlotPointers>;

    AllSlotPointers bindSlots (juce::AudioProcessorValueTreeState&);

    /** Each cabinet's share of the output, indexed by slot. */
    using BlendWeights = ParamID::PerSlot<float>;

    /** The smallest share a loaded cabinet keeps, however far the dot is pushed away
        from it.

        A corner favours its cabinet heavily -- about 85% against 5% each -- but never
        silences the other three. Taking one out of the blend altogether is what the
        mute button is for, and a pad that also did it meant two controls doing the
        same job with no way to tell from the pad which one had been used. */
    inline constexpr float blendFloor = 0.2f;

    /** Where the pad's dot sits, turned into four shares that always sum to one.

        Bilinear, with a cabinet at each corner: slot 0 top-left, 1 top-right, 2
        bottom-left, 3 bottom-right, and `x`/`y` running -1 to 1 with y positive
        upwards. The centre is a quarter each; a corner is that cabinet at `blendFloor`
        short of the lot.

        Summing to one is what keeps the plugin at the same level wherever the dot is
        put, and it is the same arithmetic the old per-slot trim did for the same
        reason: four microphones on one speaker are largely correlated, so they add
        nearly arithmetically rather than as power. Four quarters and one whole come
        out at the same loudness; four halves would be six decibels up in the middle
        of the pad and nowhere else. */
    BlendWeights blendWeights (float x, float y) noexcept;

    /** A frequency, written the way somebody setting a filter reads one.

        Shared between the parameter's own text and the labels on the cut slider so the
        two cannot disagree. The rule is that the digits stop where the ear does: nobody
        is placing a cut at 6.35 kHz rather than 6.4, and a readout that offers to is
        one that has to be squinted at every time it moves. */
    juce::String frequencyText (float hz);

    /** How much of a response a slot keeps, in samples, and what each setting is
        called.

        Counts of samples because that is what a cabinet capture is measured in. Two
        thousand is past where a speaker in a box has stopped moving, so it is what a
        slot does unless it is told otherwise. High is for a capture with the room it
        was taken in on it; Ultra is past a second, which is a reverb rather than a
        cabinet, and costs the convolution accordingly.

        Held as durations rather than as counts, though -- see `responseSeconds`. */
    inline constexpr std::array<int, 3> resolutions { 2048, 8192, 54000 };
    inline constexpr std::array<const char*, 3> resolutionNames { "NORMAL", "HIGH", "ULTRA" };

    /** Which one a slot starts on. */
    inline constexpr int defaultResolution = 0;

    /** Wraps an index round the tiers, which is how a control that cycles moves. */
    inline constexpr int nextResolution (int tier) noexcept
    {
        return (tier + 1) % (int) resolutions.size();
    }

    /** The rate those counts are quoted at.

        A cap in samples would be a cap on *time* that halved when a session moved from
        48 to 96 kHz, so the same file would be a different, shorter cabinet in each --
        which is the one thing a cabinet loader must not do. The numbers the control is
        named for are the counts at this rate; what is actually held is the time they
        come to. */
    inline constexpr double referenceRate = 48000.0;

    inline constexpr double responseSeconds (int tier) noexcept
    {
        const auto index = tier < 0 ? 0
                         : tier >= (int) resolutions.size() ? (int) resolutions.size() - 1
                                                            : tier;

        return (double) resolutions[(size_t) index] / referenceRate;
    }

    /** The longest any slot will keep, which is what the engine and the mix display
        both have to be prepared for. */
    inline constexpr double longestResponseSeconds =
        responseSeconds ((int) resolutions.size() - 1);

    /** The slope choices, in the order the parameter offers them, and the dB per
        octave each one means. Shared with the filters, which are designed from the
        number rather than from the index. */
    inline constexpr std::array<int, 4> slopes { 6, 12, 18, 24 };

    inline int slopeForChoice (int index) noexcept
    {
        return slopes[(size_t) juce::jlimit (0, (int) slopes.size() - 1, index)];
    }
}
