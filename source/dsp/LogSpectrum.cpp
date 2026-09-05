#include "LogSpectrum.h"

#include <array>
#include <cmath>

namespace LogSpectrum
{
    namespace
    {
        /** Half the averaging window, in octaves. A twenty-fourth either side is
            twelfth-octave averaging, which is the resolution a cabinet is normally
            looked at through -- fine enough to show the presence peak and the notches
            rather than a suggestion of them. */
        constexpr double halfWidthOctaves = 1.0 / 24.0;

        constexpr float floorDb = -120.0f;

        float toDecibels (double power) noexcept
        {
            return juce::jmax (floorDb, (float) (10.0 * std::log10 (juce::jmax (1.0e-24, power))));
        }
    }

    float frequencyForPoint (int index) noexcept
    {
        const auto proportion = (float) juce::jlimit (0, points - 1, index) / (float) (points - 1);

        return lowestHz * std::pow (highestHz / lowestHz, proportion);
    }

    int fftSizeFor (int numSamples) noexcept
    {
        // Eight times the response, so its spectrum is sampled densely enough to draw
        // a line through rather than a staircase -- and never below the floor, which
        // is what a short cabinet capture needs most.
        auto size = minimumFftSize;

        while (size < numSamples * 8 && size < maximumFftSize)
            size <<= 1;

        return size;
    }

    void sample (const float* magnitudes, int numBins, double binWidthHz,
                 std::vector<float>& destinationDb)
    {
        destinationDb.assign ((size_t) points, floorDb);

        if (magnitudes == nullptr || numBins < 2 || binWidthHz <= 0.0)
            return;

        const auto ratio = std::pow (2.0, halfWidthOctaves);
        const auto highestBin = numBins - 1;

        for (int point = 0; point < points; ++point)
        {
            const auto frequency = (double) frequencyForPoint (point);

            // The window this point averages over, in bins.
            //
            // Never narrower than two bins, and that floor is what removes the
            // staircase. A twelfth-octave window at twenty hertz is about a bin and a
            // half wide, and a window narrower than the bins it lands on can only
            // report whichever bin it happens to sit in -- so the dozen display points
            // that share that bin all report the same value. Widening it to two bins
            // costs nothing real (at the bottom of the axis two bins is finer than the
            // response's own resolution) and makes the window always straddle a pair,
            // so the answer moves continuously as the window slides between them.
            const auto centre = frequency / binWidthHz;
            const auto halfWidth = juce::jmax (1.0, centre * (ratio - 1.0 / ratio) * 0.5);

            const auto from = centre - halfWidth;
            const auto to = centre + halfWidth;

            const auto first = juce::jlimit (1, highestBin, (int) std::floor (from));
            const auto last = juce::jlimit (1, highestBin, (int) std::ceil (to) - 1);

            auto sum = 0.0;
            auto weight = 0.0;

            for (int bin = first; bin <= last; ++bin)
            {
                // How much of this bin's width lies inside the window, 0 to 1.
                //
                // Fractional, rather than counting whole bins in or out. Rounding makes
                // the average a step function of frequency all over again: neighbouring
                // points whose windows round to the same pair of bins get identical
                // values, which is the same staircase further up the axis and smaller.
                const auto overlap = juce::jmin (to, (double) bin + 1.0)
                                   - juce::jmax (from, (double) bin);

                if (overlap <= 0.0)
                    continue;

                const auto magnitude = (double) magnitudes[bin];
                sum += magnitude * magnitude * overlap;
                weight += overlap;
            }

            destinationDb[(size_t) point] = weight > 0.0 ? toDecibels (sum / weight) : floorDb;
        }
    }

    void applyPinkTilt (std::vector<float>& curve)
    {
        // Three decibels per doubling is ten times the log of the ratio, and pivoting
        // about a kilohertz keeps the tilt centred on the graph rather than lifting the
        // whole trace off the top of it.
        constexpr double pivotHz = 1000.0;

        for (int point = 0; point < (int) curve.size(); ++point)
        {
            const auto frequency = (double) frequencyForPoint (point);

            curve[(size_t) point] += (float) (10.0 * std::log10 (frequency / pivotHz));
        }
    }

    void smooth (std::vector<float>& curve)
    {
        static constexpr std::array<float, 7> kernel { 1.0f, 6.0f, 15.0f, 20.0f, 15.0f, 6.0f, 1.0f };
        static constexpr float kernelSum = 64.0f;

        const auto count = (int) curve.size();

        if (count < (int) kernel.size())
            return;

        std::vector<float> smoothed ((size_t) count);

        for (int point = 0; point < count; ++point)
        {
            auto sum = 0.0f;

            for (int tap = 0; tap < (int) kernel.size(); ++tap)
            {
                const auto index = juce::jlimit (0, count - 1, point + tap - (int) kernel.size() / 2);
                sum += curve[(size_t) index] * kernel[(size_t) tap];
            }

            smoothed[(size_t) point] = sum / kernelSum;
        }

        curve = std::move (smoothed);
    }
}
