#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

/**
    The mapping between the graph's rectangle and what is drawn in it: frequency along
    a logarithmic x axis, and decibels down the y.

    A type rather than a set of loose functions because everything that draws on the
    graph needs all of it, and passing the rectangle to each one separately meant every
    caller re-derived the same mappings and could disagree about them.

    The dB window is the mockup's exactly -- zero at the top, eighty-four below it,
    labelled every twelve. Seven divisions rather than a round eight, because the
    labels have to land *on* the gridlines: pick a range and a division count that do
    not divide evenly and every label sits half a line out.
*/
struct PlotGeometry
{
    juce::Rectangle<float> bounds;

    //==========================================================================
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;

    static constexpr float topDb = 0.0f;
    static constexpr float floorDb = -84.0f;
    static constexpr int gridDivisions = 7;   // 12 dB apiece

    //==========================================================================
    // The margins a display keeps around its plot for the axis furniture: the decibel
    // scale down the left, the frequencies or milliseconds along the bottom.
    //
    // Here rather than in each display, because they are what decides where the plot
    // *is* -- and anything positioned against the plot rather than against the
    // component has to be able to ask. They were declared separately and identically
    // in both displays, which is two places for one fact.

    static constexpr float axisLeft = 38.0f;
    static constexpr float axisRight = 12.0f;
    static constexpr float axisTop = 10.0f;
    static constexpr float axisBottom = 22.0f;

    /** The plotting rectangle inside a display occupying `bounds` -- the region the
        gridlines enclose, whose edges are the 0 dB line at the top and the 20 kHz (or
        20 ms) line at the right. */
    static juce::Rectangle<float> plotAreaWithin (juce::Rectangle<float> bounds) noexcept
    {
        return bounds.withTrimmedLeft (axisLeft)
                     .withTrimmedRight (axisRight)
                     .withTrimmedTop (axisTop)
                     .withTrimmedBottom (axisBottom);
    }

    //==========================================================================
    // The rectangle's own accessors, forwarded, so drawing code can ask this one
    // object where the plot is as well as what a value means in it.
    float getX() const noexcept       { return bounds.getX(); }
    float getY() const noexcept       { return bounds.getY(); }
    float getRight() const noexcept   { return bounds.getRight(); }
    float getBottom() const noexcept  { return bounds.getBottom(); }
    float getWidth() const noexcept   { return bounds.getWidth(); }
    float getHeight() const noexcept  { return bounds.getHeight(); }
    float getCentreX() const noexcept { return bounds.getCentreX(); }
    float getCentreY() const noexcept { return bounds.getCentreY(); }

    bool contains (juce::Point<float> point) const noexcept { return bounds.contains (point); }

    //==========================================================================
    float freqToX (float hz) const noexcept
    {
        return bounds.getX() + freqProportion (hz) * bounds.getWidth();
    }

    float xToFreq (float x) const noexcept
    {
        if (bounds.getWidth() <= 0.0f)
            return minFreq;

        const auto proportion = juce::jlimit (0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
        return std::pow (10.0f, logMin() + proportion * (logMax() - logMin()));
    }

    float dbToY (float db) const noexcept
    {
        const auto clamped = juce::jlimit (floorDb, topDb, db);
        return proportionToY ((clamped - floorDb) / (topDb - floorDb));
    }

    /** 0 at the bottom of the plot, 1 at the top. */
    float proportionToY (float proportion) const noexcept
    {
        return bounds.getBottom() - proportion * bounds.getHeight();
    }

    /** Where a proportion of the width lands, which is what the waveform view plots
        time against -- it shares this type for the rectangle and the axis furniture,
        not for the frequency mapping. */
    float proportionToX (float proportion) const noexcept
    {
        return bounds.getX() + juce::jlimit (0.0f, 1.0f, proportion) * bounds.getWidth();
    }

    /** The decade-and-a-bit marks a frequency axis is read by. Shared so the grid and
        anything labelling it cannot disagree about which lines get a number. */
    static constexpr std::array<float, 27> gridFrequencies {
        20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f,
        100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f,
        1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f,
        10000.0f
    };

    /** Which of those carry a label. Every line labelled is a wall of numbers; the
        octave-ish ones are what an eye actually navigates by. */
    static bool isLabelled (float hz) noexcept
    {
        for (const auto labelled : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                     1000.0f, 2000.0f, 5000.0f, 10000.0f })
            if (juce::approximatelyEqual (hz, labelled))
                return true;

        return false;
    }

    static juce::String label (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz < 10000.0f ? 0 : 0) + "k";

        return juce::String ((int) hz);
    }

private:
    static float logMin() noexcept { return std::log10 (minFreq); }
    static float logMax() noexcept { return std::log10 (maxFreq); }

    static float freqProportion (float hz) noexcept
    {
        const auto clamped = juce::jlimit (minFreq, maxFreq, hz);
        return (std::log10 (clamped) - logMin()) / (logMax() - logMin());
    }
};
