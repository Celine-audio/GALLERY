#include "MultiWaveformDisplay.h"

#include "Fonts.h"
#include "PlotGeometry.h"
#include "Theme.h"

using namespace Celine;

//==============================================================================
MultiWaveformDisplay::MultiWaveformDisplay()
{
    setInterceptsMouseClicks (false, false);
}

void MultiWaveformDisplay::setTrace (int index, Trace trace)
{
    if (! juce::isPositiveAndBelow (index, numTraces))
        return;

    traces[(size_t) index] = std::move (trace);
    repaint();
}

void MultiWaveformDisplay::clearTrace (int index)
{
    if (! juce::isPositiveAndBelow (index, numTraces))
        return;

    traces[(size_t) index] = {};
    repaint();
}

void MultiWaveformDisplay::setTimeSpan (double seconds)
{
    const auto wanted = juce::jmax (1.0e-4, seconds);

    if (juce::approximatelyEqual (wanted, timeSpan))
        return;

    timeSpan = wanted;
    repaint();
}

void MultiWaveformDisplay::setZoomed (bool shouldZoom)
{
    if (zoomed == shouldZoom)
        return;

    zoomed = shouldZoom;
    repaint();
}

int MultiWaveformDisplay::getPlotWidth() const noexcept
{
    return juce::jmax (1, getWidth() - (int) (PlotGeometry::axisLeft + PlotGeometry::axisRight));
}

//==============================================================================
juce::Rectangle<float> MultiWaveformDisplay::plotArea() const
{
    return PlotGeometry::plotAreaWithin (getLocalBounds().toFloat());
}

//==============================================================================
void MultiWaveformDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::consoleBackground());

    drawTimeAxis (g);

    // One scale for all four, set by the loudest of them.
    //
    // The responses reaching this display are levelled so their loudest *frequency*
    // sits at unity, which is what stops a cabinet from changing how loud the track is
    // -- and which leaves the individual samples of a long capture very small indeed.
    // Drawn against the absolute scale they were, four cabinets came out as a flat line
    // with a wobble in it. Sharing one scale rather than normalising each trace
    // separately is the part that matters: a cabinet turned down has to still look
    // turned down.
    auto loudest = 0.0f;

    for (const auto& trace : traces)
    {
        if (! trace.visible)
            continue;

        for (const auto& column : trace.columns)
            loudest = juce::jmax (loudest, std::abs (column.low), std::abs (column.high));
    }

    const auto scale = loudest > 1.0e-6f ? 0.92f / loudest : 1.0f;

    for (int i = 0; i < numTraces; ++i)
        drawTrace (g, i, scale);
}

void MultiWaveformDisplay::drawTimeAxis (juce::Graphics& g) const
{
    const auto plot = plotArea();

    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    g.setFont (Fonts::light (10.0f));

    // A round number of milliseconds per division, chosen so the labels stay readable
    // whether the view is showing eight milliseconds or two hundred.
    const auto totalMs = timeSpan * 1000.0;

    auto step = 1.0;

    for (const auto candidate : { 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 })
    {
        step = candidate;

        if (totalMs / candidate <= 10.0)
            break;
    }

    for (auto ms = 0.0; ms <= totalMs + 1.0e-6; ms += step)
    {
        const auto x = plot.getX() + (float) (ms / totalMs) * plot.getWidth();

        if (x > plot.getRight() + 1.0f)
            break;

        g.setColour (Theme::grid().withAlpha (0.28f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        g.setColour (Theme::comment());
        g.drawText (juce::String (ms, ms < 10.0 && step < 1.0 ? 1 : 0),
                    juce::Rectangle<float> (x - 22.0f, plot.getBottom() + 4.0f, 44.0f, 14.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    // The centre line, which is what a signed waveform is read against.
    g.setColour (Theme::grid().withAlpha (0.5f));
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

    g.setColour (Theme::comment());
    g.drawText ("ms", juce::Rectangle<float> (2.0f, plot.getBottom() + 4.0f, PlotGeometry::axisLeft, 14.0f).toNearestInt(),
                juce::Justification::centredLeft, false);
}

void MultiWaveformDisplay::drawTrace (juce::Graphics& g, int index, float scale) const
{
    const auto& trace = traces[(size_t) index];

    if (! trace.visible || trace.columns.empty() || trace.seconds <= 0.0)
        return;

    const auto plot = plotArea();

    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    const auto count = (int) trace.columns.size();

    // How much of the axis this trace covers. A capture shorter than the view stops
    // part way across rather than being stretched to fill it -- stretching would put
    // two responses of different lengths on two different time axes, drawn as though
    // they shared one, which is the one thing this display must never do.
    const auto covered = juce::jlimit (0.0, 1.0, trace.seconds / timeSpan);
    const auto begins = juce::jlimit (0.0, 1.0, trace.startSeconds / timeSpan);
    const auto polarity = trace.inverted ? -1.0f : 1.0f;
    const auto height = plot.getHeight() * 0.5f * scale;

    const auto pointAt = [&] (int i, bool top)
    {
        const auto clamped = juce::jlimit (0, count - 1, i);
        const auto proportion = begins
                              + (double) clamped / (double) juce::jmax (1, count - 1) * covered;
        const auto column = trace.columns[(size_t) clamped];

        return juce::Point<float> (plot.getX() + (float) juce::jlimit (0.0, 1.0, proportion)
                                                     * plot.getWidth(),
                                   plot.getCentreY() - (top ? column.high : column.low)
                                                           * polarity * height);
    };

    juce::Path outline;

    // Down the tops and back along the bottoms, so the body of the waveform is one
    // closed shape that can be filled behind the four of them without leaving seams --
    // and drawn through the midpoints as curves rather than corner to corner, which is
    // what a sampled signal actually looks like between its samples.
    //
    // A cabinet pushed back by alignment starts along the centre line rather than in
    // mid-air. Those samples are not missing -- they are silence, which is a thing this
    // view has to draw: a trace that simply began part way across would read as a
    // shorter response rather than as a later one, which is the opposite of what the
    // control did.
    const auto startsAt = plot.getX() + (float) begins * plot.getWidth();

    if (begins > 0.0)
    {
        // Flat along the axis to where the cabinet arrives, then into it. Straight to
        // the first sample instead would draw a ramp out of nothing, which is a slope
        // the response does not have.
        outline.startNewSubPath (plot.getX(), plot.getCentreY());
        outline.lineTo (startsAt, plot.getCentreY());
        outline.lineTo (pointAt (0, true));
    }
    else
    {
        outline.startNewSubPath (pointAt (0, true));
    }

    for (int i = 1; i < count; ++i)
    {
        const auto previous = pointAt (i - 1, true);
        const auto current = pointAt (i, true);

        outline.quadraticTo (previous, (previous + current) * 0.5f);
    }

    outline.lineTo (pointAt (count - 1, true));
    outline.lineTo (pointAt (count - 1, false));

    for (int i = count - 1; --i >= 0;)
    {
        const auto previous = pointAt (i + 1, false);
        const auto current = pointAt (i, false);

        outline.quadraticTo (previous, (previous + current) * 0.5f);
    }

    outline.lineTo (pointAt (0, false));

    if (begins > 0.0)
    {
        outline.lineTo (startsAt, plot.getCentreY());
        outline.lineTo (plot.getX(), plot.getCentreY());
    }

    outline.closeSubPath();

    // A line, drawn twice -- out along the tops and back along the bottoms.
    //
    // No fill. At the resolution this display asks for there is about one sample to a
    // column, so the two envelopes are the same line and a fill between them has no
    // area to cover; what it did instead was wash the space between each trace and the
    // centre line, which is what fused the four together. Four thin lines is what a
    // comparison of four waveforms wants to be.
    g.setColour (trace.colour);
    g.strokePath (outline, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}
