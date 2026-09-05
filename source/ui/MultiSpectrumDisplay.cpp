#include "MultiSpectrumDisplay.h"

#include "Fonts.h"
#include "PlotGeometry.h"
#include "Theme.h"

using namespace Celine;

//==============================================================================
MultiSpectrumDisplay::MultiSpectrumDisplay()
{
    setInterceptsMouseClicks (false, false);
}

void MultiSpectrumDisplay::setTrace (int index, const std::vector<float>& decibels,
                                     juce::Colour colour, bool visible)
{
    if (! juce::isPositiveAndBelow (index, numTraces))
        return;

    auto& trace = traces[(size_t) index];

    trace.decibels = decibels;
    trace.colour = colour;
    trace.visible = visible && ! decibels.empty();

    repaint();
}

void MultiSpectrumDisplay::clearTrace (int index)
{
    if (! juce::isPositiveAndBelow (index, numTraces))
        return;

    traces[(size_t) index] = {};
    repaint();
}

//==============================================================================
juce::Rectangle<float> MultiSpectrumDisplay::plotArea() const
{
    return PlotGeometry::plotAreaWithin (getLocalBounds().toFloat());
}

//==============================================================================
void MultiSpectrumDisplay::setMixTrace (const std::vector<float>& decibels)
{
    mix.decibels = decibels;
    mix.colour = Theme::text();
    mix.visible = decibels.size() >= 2;

    repaint();
}

void MultiSpectrumDisplay::setOutputSpectrum (const std::vector<float>& decibels)
{
    if (output == decibels)
        return;

    output = decibels;
    repaint();
}

//==============================================================================
void MultiSpectrumDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::consoleBackground());

    drawGrid (g);
    drawOutput (g);

    for (int i = 0; i < numTraces; ++i)
        drawTrace (g, traces[(size_t) i], 1.8f, 0.06f);

    // Last, so it reads on top of the four when both are being shown. Heavier and with
    // a stronger wash, because it is the answer rather than one of the workings.
    drawTrace (g, mix, 2.6f, 0.12f);
}

void MultiSpectrumDisplay::drawOutput (juce::Graphics& g) const
{
    if (output.size() < 2)
        return;

    const PlotGeometry plot { plotArea() };

    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    const auto count = (int) output.size();

    juce::Path filled;
    filled.startNewSubPath (plot.getX(), plot.getBottom());

    for (int i = 0; i < count; ++i)
    {
        const auto x = plot.proportionToX ((float) i / (float) (count - 1));
        filled.lineTo (x, plot.dbToY (output[(size_t) i]));
    }

    filled.lineTo (plot.getRight(), plot.getBottom());
    filled.closeSubPath();

    // A solid it fills up to rather than a line it draws. The four cabinets are lines;
    // making this one too would put five lines on a graph meant to compare four.
    g.setColour (Theme::comment().withAlpha (0.35f));
    g.fillPath (filled);
}

void MultiSpectrumDisplay::drawGrid (juce::Graphics& g) const
{
    const PlotGeometry plot { plotArea() };

    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    g.setFont (Fonts::light (10.0f));

    // Frequency, up the plot. Every decade step gets a line; only the ones an eye
    // navigates by get a number, or the axis becomes a wall of digits.
    for (const auto hz : PlotGeometry::gridFrequencies)
    {
        const auto labelled = PlotGeometry::isLabelled (hz);
        const auto x = plot.freqToX (hz);

        g.setColour (Theme::grid().withAlpha (labelled ? 0.5f : 0.22f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        if (! labelled)
            continue;

        g.setColour (Theme::comment());
        g.drawText (PlotGeometry::label (hz),
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 14.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    // Decibels, across it.
    for (int division = 0; division <= PlotGeometry::gridDivisions; ++division)
    {
        const auto db = PlotGeometry::topDb
                      + (PlotGeometry::floorDb - PlotGeometry::topDb)
                            * (float) division / (float) PlotGeometry::gridDivisions;

        const auto y = plot.dbToY (db);

        g.setColour (Theme::grid().withAlpha (division == 0 ? 0.5f : 0.22f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

        g.setColour (Theme::comment());
        g.drawText (juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (0.0f, y - 7.0f, PlotGeometry::axisLeft - 6.0f, 14.0f).toNearestInt(),
                    juce::Justification::centredRight, false);
    }

    g.setColour (Theme::comment());
    g.drawText ("dBFS", juce::Rectangle<float> (2.0f, plot.getBottom() + 4.0f, PlotGeometry::axisLeft, 14.0f).toNearestInt(),
                juce::Justification::centredLeft, false);
}

void MultiSpectrumDisplay::drawTrace (juce::Graphics& g, const Trace& trace,
                                      float thickness, float fillAlpha) const
{
    if (! trace.visible || trace.decibels.size() < 2)
        return;

    const PlotGeometry plot { plotArea() };

    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    const auto count = (int) trace.decibels.size();

    const auto pointAt = [&] (int point)
    {
        const auto clamped = juce::jlimit (0, count - 1, point);

        return juce::Point<float> (plot.proportionToX ((float) clamped / (float) (count - 1)),
                                   plot.dbToY (trace.decibels[(size_t) clamped]));
    };

    juce::Path curve;
    curve.startNewSubPath (pointAt (0));

    // Through the midpoints, with each measured point as the control it bends around.
    //
    // Straight segments between five hundred points draw a corner at every one of
    // them, and a cabinet's response has enough fine structure that the result reads
    // as noise on the trace rather than as detail in the cabinet. This is a drawing
    // decision and nothing else -- it does not touch what the measurement said, and
    // it is deliberately gentler than the Smooth control, which does.
    for (int i = 1; i < count; ++i)
    {
        const auto previous = pointAt (i - 1);
        const auto current = pointAt (i);
        const auto midpoint = (previous + current) * 0.5f;

        curve.quadraticTo (previous, midpoint);
    }

    curve.lineTo (pointAt (count - 1));

    // A wash under the curve as well as the curve itself. With four of them overlaid
    // the fills tell you which is on top where they cross, which the strokes alone do
    // not -- and kept faint, because four opaque fills would be mud.
    auto filled = curve;
    filled.lineTo (plot.getRight(), plot.getBottom());
    filled.lineTo (plot.getX(), plot.getBottom());
    filled.closeSubPath();

    g.setColour (trace.colour.withAlpha (fillAlpha));
    g.fillPath (filled);

    g.setColour (trace.colour);
    g.strokePath (curve, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}
