#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/**
    The logarithmic frequency grid both spectra are drawn on, and the one piece of
    arithmetic that puts a linear-frequency transform onto it without a staircase.

    A transform's bins are evenly spaced in hertz; this axis is evenly spaced in
    octaves. Near the bottom of it that mismatch is enormous -- at twenty hertz a
    thousand-point axis steps by about a seventh of a hertz per point, while a bin is
    several hertz wide -- so *a dozen consecutive points of the display fall inside the
    same bin*. Reading the nearest bin, or averaging a rounded range of them, gives
    those twelve points the same value: a flat tread, then a step to the next. That is
    the staircase, and it is in the data rather than in the drawing, so no amount of
    curve fitting takes it out.

    The answer is to treat the bins as samples of a continuous function, which is what
    they are, and to read *between* them. Where the axis is coarser than the bins there
    are several to average and averaging is right; where it is finer there are none to
    average and the two neighbours are interpolated instead.
*/
namespace LogSpectrum
{
    /** How finely the axis is sampled. A thousand points over three decades is finer
        than the graph has pixels at any size this window opens at, so what is drawn is
        limited by the display rather than by the measurement. */
    inline constexpr int points = 1024;

    inline constexpr float lowestHz = 20.0f;
    inline constexpr float highestHz = 20000.0f;

    /** The frequency point `index` stands for. */
    float frequencyForPoint (int index) noexcept;

    /** The smallest transform worth measuring a response with.

        Not about the response's own resolution -- a hundred-millisecond capture knows
        nothing finer than ten hertz whatever is done to it -- but about how densely
        that resolution is *sampled*. Zero-padding a transform is exact interpolation
        of the spectrum it would have had, so padding well past the response's length
        is what turns a handful of bins across the bottom octaves into enough of them
        to draw a line through. */
    inline constexpr int minimumFftSize = 1 << 16;
    inline constexpr int maximumFftSize = 1 << 17;

    /** Chooses a transform size for a response of `numSamples`: enough padding to
        sample its spectrum densely, and never less than `minimumFftSize`. */
    int fftSizeFor (int numSamples) noexcept;

    /** Reads `magnitudes` -- one per bin, evenly spaced by `binWidthHz` -- onto the
        grid above, in decibels.

        `destinationDb` is resized to `points`. Bins below the first are never read:
        bin zero is the response's net area rather than a frequency. */
    void sample (const float* magnitudes, int numBins, double binWidthHz,
                 std::vector<float>& destinationDb);

    /** A gentle pass along a sampled curve, for the live trace -- whose ripple is
        measurement noise with nothing underneath it. Deliberately *not* used on a
        measured response, where the ripple is the cabinet. */
    void smooth (std::vector<float>& curve);

    /** Tilts a measured curve by 3 dB per octave, pivoting about a kilohertz.

        The analyser's convention, and the reason is what music looks like: broadband
        programme material falls at roughly three decibels an octave, so drawn flat it
        slopes off the bottom right of every graph it is put on and says nothing about
        the top end at all. Tilted, pink noise reads as a horizontal line and what is
        left on screen is how the signal differs from that.

        For measured *signals* only. A cabinet's frequency response is a filter, and
        tilting a filter's curve would draw a shape it does not have. */
    void applyPinkTilt (std::vector<float>& curve);
}
