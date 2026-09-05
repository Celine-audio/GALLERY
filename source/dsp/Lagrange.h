#pragma once

#include <array>
#include <cmath>

/**
    Reading a signal between two of its samples, stated once.

    Three separate things in this plugin do it, and they have to agree. Alignment is a
    delay line on the audio thread, `juce::dsp::DelayLine` with `Lagrange3rd`; the mix
    curve adds each cabinet in at that same delay; and a response recorded at one rate
    is resampled on the way into a session at another. A picture that interpolated
    differently from the audio would be a picture of something else, and a resampler
    that interpolated worse would make the same file a duller cabinet at 44.1 kHz than
    at 48 -- which is the one thing holding a response as a duration is meant to prevent.

    Third order rather than linear, and the difference is not subtle: measured on a
    48 kHz sine read into a 44.1 kHz session, linear interpolation lost 1.8 dB at 12 kHz
    and 3.0 dB at 16 kHz. Linear interpolation of a fractional sample is a low-pass whose
    corner moves with the fraction, so it also makes *how far* a cabinet has been aligned
    change its tone.
*/
namespace Lagrange
{
    /** The four weights for reading at `fraction` of the way from `s[n]` to `s[n + 1]`.

        `weights(f)[k]` multiplies `s[n - 1 + k]`, so the kernel reaches one sample
        before the pair and two after it. At a fraction of nothing it is exactly
        `{ 0, 1, 0, 0 }`: a read at a whole sample returns that sample untouched. */
    inline constexpr std::array<float, 4> weights (float fraction) noexcept
    {
        const auto f = fraction;

        return { -f * (f - 1.0f) * (f - 2.0f) / 6.0f,
                 (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f,
                 -(f + 1.0f) * f * (f - 2.0f) * 0.5f,
                 (f + 1.0f) * f * (f - 1.0f) / 6.0f };
    }

    /** `data` read at a fractional index, with the ends held rather than wrapped. A
        response starts and ends on silence, so holding them changes nothing; wrapping
        would fold its tail onto its head. */
    inline float read (const float* data, int numSamples, double position) noexcept
    {
        if (numSamples <= 0)
            return 0.0f;

        const auto base = (int) std::floor (position);
        const auto taps = weights ((float) (position - (double) base));

        auto sum = 0.0f;

        for (int k = 0; k < 4; ++k)
        {
            const auto index = juce::jlimit (0, numSamples - 1, base - 1 + k);
            sum += taps[(size_t) k] * data[index];
        }

        return sum;
    }
} // namespace Lagrange
